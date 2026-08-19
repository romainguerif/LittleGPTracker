The wrong-file line numbers (575 is in SampleInstrument, not AudioMixer) don't matter. I have what I need. The envelope-RELEASE-on-Stop mechanism is confirmed (lines 498-500, 1084-1086): a Stop() with default release 0 kills the voice in one K-rate tick. That is the *cut mechanism*. The question is whether the *trigger* is thread-positional or channel-indexed.

Two reports said "channel-specific" pointing at an external early Stop() on 8-15 (but neither found such a call — the note-length/sequencer agent explicitly ruled it out, finding NO channel-index-dependent Stop). Four reports converge on the worker thread split as the only thing segregating 8-15. The decisive fact: the note-length/sequencer agent traced ch8 vs ch0 step by step and found them byte-symmetric — there is NO early note-off for 8-15. So the "external early Stop on 8-15" theory has no actual caller. That collapses the channel-specific hypothesis.

I have enough to converge decisively. Let me write the synthesis.

# Diagnostic — Notes des pistes 9-16 coupées net après l'attaque

## 1. Verdict : c'est un bug **DE THREAD** (le worker multicœur), pas un bug de canal

Le bug est **spécifique au thread worker multicœur**, pas aux canaux 8-15 en eux-mêmes.

**Pourquoi c'est tranché et non « 50/50 » :**

- Les pistes 9-16 = canaux 8-15 = bus 8-16. Or `sumChildrenParallel` (AudioMixer.cpp:237) calcule `half = nc/2`. Avec les 17 bus toujours insérés dans le master (MixerService.cpp:38-39, `MAX_BUS_COUNT=17`), `half=8` **à chaque bloc, de façon déterministe**. Le thread audio rend les enfants [0,8) = pistes 1-8 ; le worker rend [8,17) = **pistes 9-16** + le bus preview. La frontière du bug coïncide **exactement** avec la frontière du split de thread.
- `useMulticore` est **activé par défaut** (MixerService.cpp:44-45 : vrai sauf si `AUDIOMULTICORE=NO`).
- **4 agents sur 6** (note-length-and-sequencer, exhaustive-channel-array-audit, render-bounds-and-worker-thread dans son verdict factuel, bus-volume-mute-delay-send) convergent indépendamment sur ce même split comme seule chose qui sépare 8-15. C'est une convergence forte.
- **Les 2 agents qui ont dit « channel-specific »** (amp-envelope, render-bounds) pointent tous deux vers un *Stop() précoce externe sur 8-15* — **mais aucun n'a trouvé l'appelant**. Et l'agent note-length-and-sequencer, qui a tracé pas à pas le canal 8 contre le canal 0 dans tout le séquenceur, **a explicitement réfuté** l'existence d'un note-off précoce propre à 8-15 : aucune branche dépendante de l'index de canal, aucun tableau dimensionné à 8, aucun `&7`/`%8`. **Le déclencheur « channel-specific » n'a donc pas d'appelant réel.** Cette hypothèse s'effondre.

Le désaccord apparent se résout ainsi : le *mécanisme* de coupure (enveloppe qui tombe en RELEASE/IDLE, ou voix rendue silencieuse) est bien réel, mais son *déclencheur* est l'appartenance au thread worker, pas l'index de canal.

L'audit exhaustif des tableaux est **propre** : `SONG_CHANNEL_COUNT=16` (Song.h:8, définition unique), tous les tableaux par canal dimensionnés à 16, aucun stride codé en dur à 8 dans le chemin audio. Le bug **n'est pas** un dépassement de tableau.

---

## 2. Cause racine la plus probable : le worker ne tient pas le temps réel → la moitié worker est rendue muette/à zéro

**Le mécanisme exact.** Le code lui-même avoue le symptôme. Lisez le commentaire que l'auteur a laissé dans le worker, AudioMixer.cpp:22-35 :

> *« this helper renders the master's SECOND HALF of buses (channels 9-16 + the preview stream). Without the flush, decaying sample tails / EQ / feedback drift into denormals, which are ~100x slower on Cortex-A53. The worker then can't keep up, the audio thread stalls at the barrier, and exactly those worker-rendered voices "play their start then cut". »*

C'est **textuellement le symptôme**. L'auteur avait identifié la bonne zone (le worker qui n'arrive pas à suivre → stall à la barrière → les voix de la moitié worker « jouent leur début puis coupent ») et a tenté de corriger via le flush denormal FPCR.FZ (lignes 30-35). **Mais d'après les rapports, ce flush n'a PAS corrigé le bug.**

Le chemin de coupure côté audio est précis et confirmé :
1. Le worker reçoit `go_->Post()` (AudioMixer.cpp:244), le thread audio rend sa moitié, puis attend à la barrière `worker_->done_->Wait()` (ligne 250).
2. Si le worker **ne finit pas son bloc à temps** (surcharge CPU, denormals résiduels, ou simplement charge de 8 voix + EQ + feedback sur un cœur lent), le scratch des enfants 8-16 n'est **pas rempli pour ce bloc**.
3. La somme finale (lignes 255-261) n'ajoute `childScratch_[i]` que si `childGotData_[i]` est vrai — sinon ce bus contribue **du silence** ce bloc-là. Des blocs muets répétés sur 8-15 = « attaque puis coupure ».

**Pourquoi seulement 8-15 :** parce que ce sont précisément les bus assignés à `[half,nc)` = la moitié worker (lignes 241-242). Les pistes 1-8 sont rendues en ligne sur le thread audio et ne dépendent jamais de la barrière, donc elles sustainent toujours.

**Le défaut de fond** est dans la conception du handshake : la barrière `done_->Wait()` à la ligne 250 est **bloquante mais sans garantie de timing**, et surtout le coût de rendu est **déséquilibré** — la moitié worker porte les bus lourds (instruments + EQ + feedback + delay send) plus le bus preview, sur un seul cœur Cortex-A53, alors que le budget temps réel par bloc audio est fixe. Le flush FPCR a réduit *un* facteur de ralentissement (denormals) mais pas la surcharge structurelle.

**Fichier:ligne du cœur du problème :** `/Users/romain/Desktop/LGPT/LittleGPTracker/sources/Services/Audio/AudioMixer.cpp:237` (`int half=nc/2`) et la boucle de rendu/barrière worker lignes 41-50 + 244-261.

**Note importante sur le mécanisme de coupure d'enveloppe** (SampleInstrument.cpp:498-500 et 1084-1086) : avec un release par défaut à 0, un Stop() tue la voix en un tick K-rate (~2,3 ms). Ce mécanisme est réel **mais il n'est PAS le déclencheur ici**, car aucun Stop() spécifique à 8-15 n'existe (réfuté par l'audit séquenceur). Ne perdez pas de temps à chasser un note-off fantôme.

---

## 3. Suspects de second rang

**A. Déséquilibre de charge du split `half=nc/2` (le même fichier, angle « répartition » plutôt que « race »).** Location : AudioMixer.cpp:237. Plausible car la moitié worker (8 bus pistes + preview) est plus lourde que la moitié audio, et un cœur lent peut rater le budget bloc de façon répétée → coupures. Moins « distinct » que le #1 parce que c'est en réalité **la même racine** vue sous un autre angle ; je le liste pour cadrer le fix (rééquilibrer ou désactiver). *Confirmer/réfuter :* mesurer le temps de `worker_` par bloc vs budget audio (un simple chrono autour de la boucle lignes 41-43, log si > durée bloc).

**B. Un throw transitoire dans une voix worker vidant toute la moitié (catch lignes 44-49).** Location : AudioMixer.cpp:44-48. Le `catch(...)` met `gotData_[i]=false` pour **tout** l'intervalle [first,last) sur n'importe quelle exception (ex. `bad_alloc` lors d'un `ensureChildScratch`/alloc tardif). Si quelque chose lève par bloc, **tous** les bus 8-16 deviennent muets — symptôme exact. Plausible mais moins probable que le #1 car il faudrait une exception récurrente. *Confirmer/réfuter :* ajouter un `Trace::Log` dans le bloc catch (ligne 48) ; s'il se déclenche en continu, c'est ça.

**C. Mécanisme enveloppe RELEASE→IDLE avec release=0 (SampleInstrument.cpp:1084-1086).** C'est le site **symptôme** (où le silence est effectivement produit si un Stop arrive), pas la cause. Plausible uniquement s'il existait un Stop() précoce sur 8-15 — ce qui a été réfuté. *Confirmer/réfuter :* poser un log dans `SampleInstrument::Stop(channel)` et à la ligne 1086 quand `*rpFinished=true`, avec le canal ; si Stop(8..15) ne se déclenche **jamais** tôt (attendu), ce suspect est définitivement clos et confirme que la racine est le thread.

---

## 4. Comment confirmer en 1 test (avant de toucher au code)

**Le test le moins cher et 100 % décisif :** mettre `AUDIOMULTICORE=NO` dans `config.xml` (lu en MixerService.cpp:44).

Cela force `master_.SetParallel(false)` → tous les 17 bus sont rendus séquentiellement sur le thread audio via `sumChildrenSequential` (AudioMixer.cpp:196), **sans worker, sans barrière**.

- **Si les pistes 9-16 sustainent correctement** → le bug est **100 % thread-spécifique** (le worker), CQFD. C'est le résultat attendu.
- **Si elles coupent encore** → le bug serait channel-spécifique (à chercher dans l'état par canal), mais toutes les preuves disent que ce ne sera pas le cas.

Quatre des six agents recommandent exactement ce test. C'est une simple bascule de config, zéro recompilation, réponse immédiate.

---

## 5. Le fix

### Fix immédiat (déblocage, à shipper tout de suite)

Forcer le rendu mono-cœur par défaut. Le multicœur est cassé ; tant qu'il n'est pas réparé proprement, il ne doit pas être le défaut.

Dans `MixerService.cpp:44-45`, inverser la valeur par défaut :

```cpp
// AVANT
const char *mc = Config::GetInstance()->GetValue("AUDIOMULTICORE") ;
bool useMulticore = (!mc) || (strcmp(mc,"NO")!=0 && strcmp(mc,"no")!=0) ;

// APRÈS — multicœur OFF par défaut, opt-in explicite seulement
const char *mc = Config::GetInstance()->GetValue("AUDIOMULTICORE") ;
bool useMulticore = (mc) && (strcmp(mc,"YES")==0 || strcmp(mc,"yes")==0) ;
```

Effet : par défaut tout passe par `sumChildrenSequential`, les 16 pistes sustainent. C'est le **fix correct à livrer maintenant** : le gain CPU du split ne vaut rien s'il rend la moitié des pistes inutilisables.

### Fix de fond (réparer le multicœur, à faire ensuite, pas bloquant)

Le vrai problème est que la barrière `done_->Wait()` (AudioMixer.cpp:250) suppose que le worker tient toujours le budget temps réel, et que la somme (lignes 255-261) traite un worker en retard comme du silence définitif au lieu d'un retard. Deux corrections à shipper ensemble :

1. **Rééquilibrer le split** pour que le coût soit symétrique, ou mieux, vérifier que le worker termine. Si on garde le split, instrumenter et logger les dépassements (sert aussi de confirmation du suspect A) :
   - autour de AudioMixer.cpp:41-43, chronométrer ; si `> samplecount` en temps audio, logger.

2. **Durcir le `catch`** (AudioMixer.cpp:44-49) : actuellement il vide toute la moitié worker sur n'importe quelle exception. Restreindre au minimum et **logger** systématiquement (ligne 48) pour ne pas masquer une coupure récurrente :
   ```cpp
   } catch (...) {
       Trace::Log("MixRenderWorker","worker block threw -> dropping worker half") ;
       for (int i=first_;i<last_;i++) gotData_[i]=false ;
   }
   ```
   Si ce log apparaît en boucle après réactivation, la racine réelle est le suspect B et il faudra traiter l'exception sous-jacente (probablement une allocation tardive dans `ensureChildScratch`/un Render de voix).

3. Conserver le flush FPCR.FZ déjà en place (lignes 30-35) — il est correct et utile, juste insuffisant seul.

Ne touchez **pas** à SampleInstrument, Filters, Delay, Player ni au séquenceur pour ce bug.

---

## 6. Déjà éliminé (ne pas y perdre de temps)

Vérifié et **correct** par les agents, terrain à ne pas réexplorer :

- **Dimensionnement des tableaux par canal :** `SONG_CHANNEL_COUNT=16`, définition unique (Song.h:8), aucun shadowing. `renderParams_`, `ampEnvLevel_/Phase_`, `feedback_`, `eqZ_`, `compGain_`, `lfoPhase_`, `lastMidiNote_`, `lastSample_`, `filter[16]` (Filters.cpp:12), `channelBus_`, tous les tableaux Player/PlayerMixer/TablePlayback : **tous dimensionnés à 16, indexés par le vrai `channel`**. Aucun `[8]`, `&7`, `%8`, `<<3` comme borne/stride de canal dans tout le chemin audio.
- **Séquenceur / note-length :** canal 8 tracé pas à pas contre canal 0 → **byte-symétrique**. Aucun note-off automatique, aucune commande GATE/longueur, aucune branche dépendante de l'index de canal. Les seules fins de voix (KILL, timeToLive table, Stop avant retrigger, StopChannel sur 0xFF) sont data-driven et symétriques. **Il n'y a pas de Stop() précoce sur 8-15.**
- **Migration 8→16** (Song.cpp:55-66, Project.cpp) : ne tourne que sur vieux projets, re-strides correctement, laisse 8-15=0xFF (= canal **silencieux/non démarré**, pas « démarre puis coupe »). `SONG_CHANNEL_COUNT_LEGACY=8` confiné à l'I/O fichier, jamais dans le chemin audio.
- **Filtres / EQ / compresseur / LFO :** activés par des Variables **par-instrument** (eqOn_/compOn_/lfoOn_ default false ; sustain default 0xFF = 1.0, l'enveloppe ne se coupe pas seule), donc identiques piste 1 vs piste 9. États reset à chaque note Start sans garde `<8`.
- **Bus / volume / mute / delay-send :** `channelBus_[i]=i`, tous les bus à `volume_=i2fp(1)` (unité), `mute_` false par défaut pour 8-15, delay OFF par défaut. Le delay sendBuf est dimensionné à 16 régions, borné (clamp Delay.cpp:54), et n'écrit que du wet additif — ne peut pas couper le signal sec.
- **Scratch buffers du worker :** chaque enfant écrit dans son propre `childScratch_[i]` (distinct, dimensionné `samplecount*2`), aucune écriture partagée entre threads, pas de false sharing. La visibilité mémoire est garantie (sémaphores go_/done_ = barrière complète). Les slots d'enveloppe 0-7 (thread audio) et 8-15 (worker) sont **disjoints** → pas de course sur un slot donné.
- **FPCR.FZ** déjà posé sur le worker (lignes 30-35) et le thread audio (AudioOutDriver.cpp:54-61). Le ralentissement denormal seul ne produit pas une coupure propre et déterministe, et le fix a déjà été tenté sans effet.

**En résumé : désactivez le multicœur par défaut (MixerService.cpp:45) — c'est le fix. Le worker multicœur est la racine, pas les canaux.**