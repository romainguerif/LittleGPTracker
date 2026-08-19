# Audit du système audio — LittleGPTracker / Trimui Brick

*Synthèse de 6 dimensions (robustesse temps-réel, performance DSP, chargement samples, mémoire & longs samples, démarrage & I/O, propreté du code) — 40 findings consolidés et dédupliqués.*

---

## 1. Résumé exécutif

Le système audio est **nettement plus solide qu'un fork Piggy typique** : le producteur/consommateur est propre (remplissage borné + vraie reprise sur underrun), le split multicœur est correct par construction (chaque voix écrit son propre scratch, pas de write partagé), le flush-to-zero (FTZ) est posé sur les deux threads de rendu, l'enregistrement de stems est un vrai SPSC hors-thread audio, et le parser WAV est plus robuste qu'upstream. La discipline de lock est cohérente. **Ce n'est pas un système fragile** — c'est une base saine avec des arêtes vives bien localisées.

Les **3 plus gros risques** sont : (1) le **chargement de tous les samples synchrone sur le thread UI** au démarrage d'un projet — c'est la cause racine de l'écran noir de plusieurs secondes ; (2) les **I/O disque (carte SD) exécutées dans le `Render()`** via `AudioFileStreamer` pendant le prélisting — source directe d'underruns reproductibles ; (3) plusieurs **hazards de cycle de vie multicœur** (busy-wait non-atomique qui peut figer sous -O3, deadlock audio total si une voix lève une exception, fences mémoire manquants sur A53).

Les **3 plus grosses opportunités** sont : (1) le build **n'active PAS** `-ffast-math` / `-mcpu=cortex-a53` contrairement à ce que la note projet supposait — un correctif d'une ligne pour 10-20 % de CPU audio en moins ; (2) le chargement des samples est I/O-bound de façon évitable (double copie, lectures par 4 Ko, copie d'import par 1000 octets) — gains de plusieurs secondes ; (3) la primitive de **streaming incrémental existe déjà** (`WavFile::GetBuffer(start,count)` + `AudioFileStreamer`), ce qui rend une vraie voix « tape » M8 réellement faisable.

**Point de vigilance important** : le plafond réel des « longs samples » aujourd'hui n'est **pas la RAM** mais la **position de lecture en `float`** qui dégrade silencieusement la qualité au-delà de **~6 minutes** (2^24 frames). C'est exactement contradictoire avec l'objectif « longs samples » et facile à mal diagnostiquer.

---

## 2. Top priorités (toutes dimensions confondues, classées par valeur/effort)

> Findings dédupliqués. Plusieurs agents ont signalé indépendamment : le foot-gun `SAMPLELOADCHUNKSIZE` (sample-loading + startup-io), la double-copie / lectures 4 Ko (sample-loading + startup-io), le commentaire « pool allocator » obsolète (3 agents), le Swap16 mal placé (startup-io + code-quality). Ils sont fusionnés ci-dessous.

| # | Item | Pourquoi ça compte | Effort | Impact | Fichier(s):ligne |
|---|------|--------------------|:------:|--------|------------------|
| **P1** | **Sortir le chargement des samples du thread UI** (worker arrière-plan + indicateur « N/total », sources non-résidentes = silence) | Cause racine de l'écran noir de plusieurs secondes à l'ouverture d'un projet lourd. Plus gros gain sur le symptôme rapporté. | **L** | UI interactive en <1 s au lieu de plusieurs secondes | `AppWindow.cpp:414-422` (`pool->Load()` :418) ; `SamplePool.cpp:45-85,127-159` |
| **P2** | **Retirer toutes les I/O disque du `Render()`** : précharger le WavFile en RAM côté GUI dans `AudioFileStreamer::Start()`, `Render()` ne fait que copier (flag atomique `ready`) | Underrun/crackle reproductible **à chaque prélisting** dans l'import, stall dur sur carte lente. Permet aussi de retirer le Lock mixer autour de `preview()`. | **M** | Élimine les underruns SD du prélisting | `AudioFileStreamer.cpp:33-91` (Open :53, GetBuffer :74) ; `PlayerMixer.cpp:31-32` ; `WavFile.cpp:289-405` |
| **P3** | **Build : ajouter `-mcpu=cortex-a53`** + compiler les 3 TU DSP avec `-ffast-math -fno-math-errno -funroll-loops` | Le build réel est `-O3` **seul** (contredit la note projet). Vectorise la somme master + delay, FMA sur EQ/comp, scheduling A53. Quasi sans changement de code. | **S** | **~10-20 % de CPU audio en moins** | `Makefile.PORTMASTER:25,27` ; TU : `SampleInstrument.cpp`, `AudioMixer.cpp`, `Delay.cpp` |
| **P4** | **Multicœur : remplacer le busy-wait `while(!IsFinished()){}`** par `SDL_WaitThread(GetOSHandle())` + rendre `shouldTerminate_`/`isFinished_` atomiques | Hang d'arrêt latent (-O3 peut hisser le load), handle SDL fuité/racé, UAF potentiel. Aujourd'hui masqué mais non documenté. | **S** | Supprime un hang/UAF à la fermeture ; fiabilise le cycle de vie multicœur | `AudioMixer.cpp:116-133` (:121) ; `Process.h:31-32` ; `Process.cpp:19,27-33` ; cf. `SDLAudioDriver.cpp:54-58` |
| **P5** | **Multicœur : try/catch autour de la boucle worker + `done_->Post()` inconditionnel** | Si une voix lève `std::bad_alloc`, l'exception sort sans poster `done_` → **deadlock audio total permanent** (redémarrage requis). Mode ON par défaut. | **S** | Transforme un hang audio permanent en au pire un bloc perdu | `AudioMixer.cpp:40-44` vs barrière :240 ; `Process.cpp:14-20` |
| **P6** | **Position de lecture 64-bit** (index frame `int64` + fraction fixe) ; widening associé des offsets octets | Plafond réel des longs samples = **~6 min** (dégradation silencieuse pitch/timing/loop), pas la RAM. Contredit directement l'objectif « longs samples ». | **M** | Lecture propre de ~6 min → multi-heures | `SampleRenderingParams.h:21` ; `SampleInstrument.cpp:820,824,1328` ; (offsets) `:821,834-839` ; `WavFile.cpp:313,325,329` |
| **P7** | **Chargement I/O samples** : (a) ne plus seek avant chaque read (`readBlock`), (b) lecture directe dans `samples_` sans memcpy pour 8/16-bit, (c) lectures par 64-256 Ko, (d) header WAV via 1 prefetch de ~128 octets | ~1000 transactions SD minuscules + double copie + lectures 4 Ko sur un 51-samples. I/O-bound évitable. Plusieurs agents convergent. | **S→M** | Chargement à froid 2-4× plus rapide (avant même l'async) | `WavFile.cpp:289-305` (seek) ; `:333-345` (copie) ; `:86-271` (header) ; `SamplePool.cpp:161,194-200` (import 1000 o) |
| **P8** | **Clamp tempo `>=10`** sur load binaire + tap, et cap `sampleCount_` à la capacité buffer | Tempo non-clampé → `playSampleCount_` dépasse les 10000 frames du buffer fixe → **overflow heap sur le thread de rendu** (crash/corruption). Atteignable via projet corrompu/tap pathologique. | **S** | Ferme un overflow heap temps-réel (crash → perte de données) | `SyncMaster.cpp:26` ; `AudioOutDriver.cpp:26` ; `Project.cpp:436-438,576-578` ; clamp UI seul `ProjectView.cpp:122-123` |
| **P9** | **Foot-gun `SAMPLELOADCHUNKSIZE`** : supprimer (ou plafonner) le `TimeService::Sleep(1)` par chunk de 4 Ko | Aujourd'hui inactif (pas de défaut sur Trimui) mais **une copie de config étrangère** rend le chargement 10-100× plus lent. `SDL_Delay(1)` arrondi au tick scheduler. Signalé par 2 agents. | **S** | Désamorce une régression de chargement latente à 1 edit de config près | `WavFile.cpp:40-46,334-347` (:346) ; `TimeService.cpp:48-50` |
| **P10** | **Fences mémoire (release/acquire)** sur les index/flags du pool audio + le ring `WavFileWriter` (actuellement plain int / `volatile`) | A53 = mémoire faible. Le consommateur peut voir l'index avancé avant le contenu du buffer → clic rare / **stem enregistré corrompu**. UB, dur à reproduire. | **S** | Supprime des clics rares A53 et des données stem périmées | `AudioDriver.h:70-76` ; `AudioDriver.cpp:62-96` ; `SDLAudioDriver.cpp:183-216` ; `WavFileWriter.h:40-41`, `.cpp:100-147` |
| **P11** | **Vérifier le retour de `SYS_MALLOC` dans `GetBuffer`** + rejeter le sample côté `SamplePool::loadSample` (au lieu de jouer un buffer vide/partiel) ; + garde « taille max sample » configurable | Sur 1 Go, quelques longs samples sur 128 slots → OOM-kill ou troncature **silencieuse**. Transforme une corruption muette en échec propre. | **S** | Message « sample trop gros » au lieu d'OOM/lecture corrompue | `WavFile.cpp:316,320-323,386` ; `SamplePool.cpp:143,127-159` |
| **P12** | **Refactorer `SampleInstrument::Render()`** (god-function ~770 lignes) : extraire les prologues comp/EQ/LFO/ADSR puis l'application par-sample en helpers ; supprimer la duplication forward/backward | Fonction la plus risquée du repo : toute modif FX = relire 770 lignes. Bloque tout test unitaire des étages DSP. | **L** | Réduit la surface de risque ; rend EQ/comp/ADSR testables | `SampleInstrument.cpp:570-1338` (prologues :681-781 ; application :1197-1290 ; dup :887-935 vs 939-985) |

---

## 3. Quick wins (faible effort / fort impact)

Actions chacune **effort S**, applicables quasi indépendamment :

1. **`Makefile.PORTMASTER:25`** — ajouter `-mcpu=cortex-a53` à `OPT_FLAGS` (un token) : 5-15 % sur le cœur in-order, gratuit. *(cf. P3)*
2. **`AudioMixer.cpp:307`** — remplacer `pow(x,3.0f)` par `x*x*x` dans `softClip` : ~20-50× moins cher pour cet op, rend le soft-clip master quasi gratuit (une ligne).
3. **`SamplePool.cpp:161`** — passer `IMPORT_CHUNK_SIZE` de 1000 à `>=65536` et sortir le buffer de la pile : ~1000× moins de syscalls à l'import.
4. **`WavFile.cpp:346`** — supprimer/gater le `Sleep(1)` par chunk : pur gain quand `SAMPLELOADCHUNKSIZE` est activé, zéro risque sinon. *(P9)*
5. **`WavFile.cpp:289-305`** — ne seek que si la position diffère (ou lectures 64-256 Ko) : 2-4× sur l'I/O SD. *(P7a)*
6. **`AudioMixer.cpp:243`** — supprimer le `memset` global dans `sumChildrenParallel`, copier le 1er enfant puis `+=` (le path séquentiel le fait déjà) ; marquer les buffers `__restrict`.
7. **`SampleInstrument.cpp:774-776`** — cacher ADSR knob→increment, recalculer seulement sur changement : ~2400 powf/s en moins en régime établi.
8. **`PersistencyService.cpp:38-41`** — `fsync` du fichier temp avant `rename` : sur ce build `_64BIT`, le `fsync` de `UnixFile::Close` est compilé out, donc une coupure de batterie peut produire un save vide → écran noir au prochain load. *(détail thème Démarrage)*
9. **`Song.cpp:94-98`** — sortir la boucle `Swap16` du `while(current)` (l'exécuter 1× après) : supprime ~8 passes redondantes sur 4080 entrées par load.
10. **Code mort** — supprimer les 2 blocs `filterize()` commentés (`Filters.cpp:82-148`) + restes OSCFINE (~120 lignes) ; corriger le commentaire « 8 filters » → `SONG_CHANNEL_COUNT` (`Filters.h:7-14`).
11. **`fixed.h`** — introduire `FP_SAMPLE_MIN/MAX` + helpers `clampSample`, remplacer les 5 copies du clamp 16-bit (surtout les floats opaques `1073709056.0f` de `satF2Fp`).

---

## 4. Détail par thème

### Robustesse temps-réel

| Sév. | Finding | Fichier:ligne | Reco |
|:----:|---------|---------------|------|
| **High** | I/O disque dans `Render()` via `AudioFileStreamer` (Open + Seek/Read SD par bloc, sur le thread worker car bus 16 ∈ 2e moitié) | `AudioFileStreamer.cpp:33-91`, `WavFile.cpp:289-405` | Précharger en RAM côté GUI, `Render()` copie seulement. *(P2)* |
| **High** | Teardown multicœur : busy-wait non-atomique → hang -O3 ; handle SDL fuité/racé ; UAF latent | `AudioMixer.cpp:116-133` ; `Process.h:31-32`, `.cpp:19` | `SDL_WaitThread` + flags `std::atomic`. *(P4)* |
| **High** | Exception dans une voix worker → `done_` jamais posté → **deadlock audio total** | `AudioMixer.cpp:40-44` vs :240 ; `Process.cpp:14-20` | try/catch + `done_->Post()` inconditionnel. *(P5)* |
| **Med** | État cross-thread (index pool, ring) en plain/`volatile` int, sans barrière sur A53 | `AudioDriver.h:70-76` ; `WavFileWriter.h:40-41` | `std::atomic` release/acquire. *(P10)* |
| **Med** | Tempo non-clampé (load/tap) → overflow heap des buffers de rendu fixes | `SyncMaster.cpp:26` ; `Project.cpp:436-438,576-578` | Clamp `[10,400]` au point d'application. *(P8)* |
| **Low** | Croissance scratch/mix qui malloc sur le thread audio quand le bloc grandit | `AudioMixer.cpp:136-144,194-197` | Pré-dimensionner au bloc max (cap 10000 frames) à l'init ; rendre `ensureChildScratch` no-op ensuite. |
| **Low** | `EnableRendering(false)` déréférence `writer_` sans null-check | `AudioMixer.cpp:150-165` (:162) | `if (writer_) {...}` + vérifier l'ouverture du WavFileWriter. |

### Performance DSP

| Sév. | Finding | Fichier:ligne | Reco |
|:----:|---------|---------------|------|
| **High** | Build sans `-ffast-math`/`-mcpu=cortex-a53`/`-funroll-loops` → réductions float non vectorisées | `Makefile.PORTMASTER:25,27` | `-mcpu=cortex-a53` global + fast-math sur les 3 TU DSP. *(P3)* |
| **Med** | Boucle interne : 2 `switch` invariants (interpolation, feedback) + math de pointeur morte par sample | `SampleInstrument.cpp:1135-1158,1162-1185` | Templatiser/spécialiser la boucle interne (instances C++03). ~10-15 % sur la voix commune. |
| **Med** | EQ/comp : round-trip int↔float **par sample** + branche par bande dans la boucle | `SampleInstrument.cpp:1249-1290` | Convertir le bloc en float 1× (option 2), traiter EQ+comp, reconvertir 1× ; bandes inactives → coeffs identité. ~30-40 % sur les voix EQ+comp. |
| **Med** | Somme master parallèle : `memset` complet + N passes d'accumulation, cache-hostile, non vectorisée | `AudioMixer.cpp:243-251` | Copier 1er enfant puis `+=`, sommer 2-4 sources/passe, `__restrict`. *(quick win #6)* |
| **Med** | Split multicœur statique moitié/moitié → un cœur idle si charge déséquilibrée ; barrière payée chaque bloc même quasi-silence ; seulement 2/4 cœurs A53 | `AudioMixer.cpp:217-253` ; `Mixer.cpp:14` | Work-stealing par compteur atomique + 3-4 workers ; fallback séquentiel si <3 voix actives. ~1.4×→~1.9×, voire 2× headroom DSP. **(plus gros effort : L)** |
| **Low** | 3 `powf` ADSR inconditionnels/bloc/voix même aux valeurs par défaut | `SampleInstrument.cpp:774-776` | Cache knob→increment ou LUT 256. *(quick win #7)* |
| **Low** | `pow(x,3.0f)` par sample dans `softClip` | `AudioMixer.cpp:307` | `x*x*x`. *(quick win #2)* |
| **Low** | `set_filter` appelé chaque bloc/tick même si params inchangés (avec un `pow(10,...)`) | `SampleInstrument.cpp:598,1020` ; `Filters.cpp:30-76` (:60) | Garde de changement sur dirt/mix + cache du `pow` bassy. |

### Chargement des samples

| Sév. | Finding | Fichier:ligne | Reco |
|:----:|---------|---------------|------|
| **High** | `Sleep(1)` par chunk si `SAMPLELOADCHUNKSIZE` set → secondes de sommeil pur/sample | `WavFile.cpp:334-347` (:346) | Supprimer / gater. Latent (pas de défaut shippé). *(P9)* |
| **Med** | Chaque octet lu dans scratch ~4 Ko puis **memcpy** dans `samples_` ; lectures par 4 Ko | `WavFile.cpp:289-305,333-347,384` | 8/16-bit : `fread` direct dans `samples_` ; 24/32/float : lire par 64-256 Ko. *(P7)* |
| **Med** | `ImportSample` copie par paires `fread/fwrite` de 1000 octets (non-puissance de 2) | `SamplePool.cpp:161,194-200` | `IMPORT_CHUNK_SIZE >= 64 Ko`, buffer hors pile ; idéalement fusionner copie+parse. *(quick win #3)* |
| **Med** | Aucun contrôle de retour `fread`/EOF ; tailles de chunk attaquant-contrôlées (gardées seulement par 64 itérations) | `WavFile.cpp:301-304,131-140,239-249,262-266` | Vérifier `fread` ; valider `position+size <= fileLength` ; clamper la taille `data`. |
| **Low** | Commentaire obsolète « pool allocator » : faux sur cette cible (`LINUXSystem::Malloc` = `malloc` simple) | `WavFile.cpp:367-372` ; `LINUXSystem.cpp:169-180` | Corriger (vrai motif = mémoire pic bornée), puis monter le chunk à 64-256 Ko. *(signalé par 3 agents)* |

### Mémoire & longs samples

| Sév. | Finding | Fichier:ligne | Reco |
|:----:|---------|---------------|------|
| **High** | Position `float` plafonne la longueur utile à **~6 min** (2^24 frames), pas la RAM — dégradation silencieuse pitch/timing/loop | `SampleRenderingParams.h:21` ; `SampleInstrument.cpp:820,824,1328` | Position 64-bit (index frame `int64` + fraction fixe). *(P6)* |
| **Med** | Math offset/taille en `int` 32-bit → overflow à ~536M frames stéréo (le plafond **suivant** après le fix float) | `SampleInstrument.cpp:821,834-839,879` ; `WavFile.cpp:313,325,329` | Widening `ptrdiff_t`/`size_t` lors du travail 64-bit ; garder `SYS_MALLOC` < `SIZE_MAX`. |
| **Med** | Aucune borne sup / garde OOM sur les loads RAM (épuisement agrégé sur 1 Go, 128 slots) | `SamplePool.cpp:143,127-159` ; `WavFile.cpp:316` ; `LINUXSystem.cpp:169` | Garde « taille max » + propager l'échec `GetBuffer`. *(P11)* |
| **Low** | Voix « tape » faisable — la primitive de lecture incrémentale bornée existe déjà | `WavFile.cpp:308-407` ; `AudioFileStreamer.cpp:33-91` ; `SampleInstrument.cpp:677,821` | Mode streaming opt-in : ring décodé hors-thread audio, v1 forward+loop simple, fallback RAM pour courts samples. **Dépend de P6.** *(effort XL)* |

### Vitesse de démarrage

| Sév. | Finding | Fichier:ligne | Reco |
|:----:|---------|---------------|------|
| **Critical** | Tous les samples chargés+décodés **synchrone sur le thread UI avant l'affichage** (cause racine écran noir) | `AppWindow.cpp:414-422` (:418) ; `SamplePool.cpp:45-85,127-159` | Rendu UI immédiat après le parse XML ; load PCM sur worker + indicateur « N/total » ; sources non-prêtes = silence. *(P1)* |
| **High** | `readBlock` seek avant chaque read → casse le read-ahead stdio sur SD lente | `WavFile.cpp:289-305` | Seek conditionnel ou lectures 64-256 Ko. *(P7a)* |
| **Med** | Header WAV parsé en ~15-25 seek+read de 2-4 octets/fichier (~1000 round-trips SD pour 51 samples) | `WavFile.cpp:86-271` | Prefetch ~128 octets, parser en mémoire. *(P7d)* |
| **Med** | Chargement 100 % série — les 4 cœurs A53 idle pendant le load | `SamplePool.cpp:60-69` | Thread pool 2-3 workers (slots indépendants). Se marie avec P1. |
| **Med** | Foot-gun `SAMPLELOADCHUNKSIZE` → `SDL_Delay(1)` par chunk 4 Ko | `WavFile.cpp:40-46,334-347` | Supprimer/désactiver sur cette cible. *(P9, doublon sample-loading)* |
| **Med** | Save atomique sans `fsync` avant `rename` → coupure batterie = save vide/garbage. `_64BIT` compile out le `fsync` de `UnixFile::Close` | `PersistencyService.cpp:25-42` ; `tinyxml.cpp:995-1003` ; `UnixFileSystem.cpp:138-144` | `fsync(temp)` avant `rename` (+ fsync du dossier). *(quick win #8)* |
| **Low** | `FileSystemService::Copy` non-atomique (pas temp+rename) + retour off-by-one (`nbwrite` part à -1) | `FileSystem.cpp:183-222` (:188,216) | Copier vers `.tmp` puis rename + fsync ; corriger le compteur. |
| **Low** | `SamplePool::Sort` O(n²) + boucle `Swap16` répétée 9× dans `Song::RestoreContent` | `SamplePool.cpp:87-104` ; `Song.cpp:94-98` | `std::sort` sur index ; sortir le Swap16. *(Swap16 = doublon code-quality)* |

### Propreté du code

| Sév. | Finding | Fichier:ligne | Reco |
|:----:|---------|---------------|------|
| **High** | `SampleInstrument::Render()` god-function ~770 lignes, 8 étages DSP inline, dup forward/backward, intestable | `SampleInstrument.cpp:570-1338` | Extraction incrémentale des prologues puis de l'application par-sample. *(P12)* |
| **Med** | Clamp 16-bit dupliqué en littéraux bruts sur 5+ sites (floats opaques inclus) | `AudioMixer.cpp:7-8` ; `Delay.cpp:97-98` ; `WavFileWriter.cpp:111-112` ; `SampleInstrument.cpp:51-56,857-858` ; `AudioFileStreamer.cpp:82-84` | `FP_SAMPLE_MIN/MAX` + `clampSample` dans `fixed.h`. *(quick win #11)* |
| **Med** | Boucle Swap16 mal placée (9×/load) ; `Swap16` = code mort PPC ; `SaveContent` ne re-swap pas | `Song.cpp:94-98,12-22,27-31` | Hoister hors boucle / supprimer l'indirection endian. *(doublon startup-io)* |
| **Med** | `MIX_BUFFER_SIZE 40000` redéfini dans 3 headers → risque de divergence silencieuse (lie delay-send / mix buffer / écriture SD) | `AudioOut.h:10` ; `AudioOutDriver.h:10` ; `DummyAudioOut.h:7` | Définir 1× dans `AudioOut.h`, inclure ailleurs. |
| **Low** | 2 blocs `filterize()` commentés + restes OSCFINE = code mort trompeur | `Filters.cpp:82-148` ; `SampleInstrument.cpp:837,919-931,969-981` | Supprimer (git garde l'historique). *(quick win #10)* |
| **Low** | Doc « 8 filters » contredit `filter[SONG_CHANNEL_COUNT]`(=16) | `Filters.h:7-14` vs `Filters.cpp:12` | Corriger le commentaire. *(quick win #10)* |
| **Low** | Défauts delay + magic `6` dupliqués sur 4 sites | `Delay.cpp:20-25` ; `Project.cpp:23-42,365-370,414` | `Delay::ResetToDefaults()` + `DELAY_BUS_VAR_COUNT`. |
| **Low** | WAV parse+convert+I/O dans une fonction ~140 lignes ; chunk-skip dupliqué ; convertisseurs intestables | `WavFile.cpp:66-271,308-407` | Extraire `skipToChunk()` + `convert24/32i/Float()` libres (testables en mémoire). |

---

## 5. Feuille de route proposée

### Maintenant (faible effort, ferme des risques réels ou débloque de gros gains)
Lot **« sécurité temps-réel + démarrage rapide, presque tout en effort S »** — peu de risque de régression, haute valeur :

- **P4** busy-wait → `SDL_WaitThread` + flags atomiques ; **P5** try/catch worker + `done_->Post()` ; **P8** clamp tempo ; **P10** fences mémoire pool+ring. *(Robustesse — désamorce hang/UAF/deadlock/overflow.)*
- **P3** flags de build (`-mcpu=cortex-a53` + fast-math sur les 3 TU DSP). *(10-20 % CPU audio, quasi gratuit — à faire tôt, ça change la donne pour tout le reste.)*
- **P7** (partie S : seek conditionnel + header prefetch + import 64 Ko) ; **P9** retrait du `Sleep`. *(Chargement 2-4× plus rapide avant même l'async.)*
- **P11** garde OOM/échec `GetBuffer` ; **quick wins #2,6,7,8** (softclip, memset, ADSR, fsync).
- Hygiène : code mort + commentaires faux + `MIX_BUFFER_SIZE` unifié + clamp `fixed.h`. *(Réduit le risque de la prochaine modif.)*

### Ensuite (effort M/L, livre les gros objectifs structurels)
- **P1** chargement samples hors thread UI + indicateur de progression *(le plus gros gain sur l'écran noir)*. **Le thread pool de load (startup-io Med) EST ce loader** — les deux convergent.
- **P2** préchargement RAM de `AudioFileStreamer` *(retire les I/O du `Render`, supprime les underruns de prélisting)*.
- **P6** position de lecture 64-bit *(plafond ~6 min → multi-heures)*.
- **P7** (partie M : lecture directe `samples_` + chunks 64-256 Ko, après correction du commentaire allocateur).
- **P12** + refactors DSP/EQ-comp *(boucle interne templatisée, round-trip int↔float amorti)* — bénéficient de P3 (fast-math) déjà en place.

> **Dépendances explicites** : P6 (position 64-bit) **doit précéder** le widening des offsets 32-bit (Mémoire Med) — sinon le fix float est re-plafonné. La voix « tape » streaming **dépend de P6** (sinon longueur re-capée à 6 min) et **réutilise P2** (préchargement/threading du streamer). P3 doit venir avant les optimisations DSP fines pour ne pas mesurer dans le vide.

### Plus tard (effort L/XL, capacités avancées)
- **Voix « tape » streaming** opt-in (Mémoire, XL) — capacité phare M8, réutilise `WavFile::GetBuffer` + le pattern worker existant. **Après P6 + P2.**
- **Multicœur dynamique** (DSP Med, L) : work-stealing + 3-4 cœurs + fallback séquentiel en passages clairsemés. ~2× de headroom DSP.
- Refactors de testabilité : extraire les convertisseurs WAV et les étages DSP en fonctions pures + tests golden-vector.

---

## 6. Ce qui est déjà solide (à NE PAS casser)

**Architecture temps-réel** — c'est le cœur sain du système, à préserver tel quel :
- **Producteur/consommateur** : remplissage borné « fill-to-target » avec watermark, `isPlaying_` gate la production dans un projet en teardown (`AudioDriver.cpp:83-96`).
- **Reprise sur underrun correcte et bien raisonnée** : sur un miss, le callback joue `miniBlank_` ET notifie le worker ; le tick moteur + flush MIDI n'ont lieu que sur les consommations de vrai buffer (`SDLAudioDriver.cpp:183-222`) → pas d'emballement séquenceur. Le bug d'over-read `miniBlank_` est déjà corrigé (:187).
- **Split multicœur sûr par construction** : chaque enfant écrit son propre scratch (pas de write partagé, pas de lock), et le delay-send par canal miroite ce design (`AudioMixer.cpp:217-253` ; `Delay.cpp:53-85`). *« Bit-identique au sommage single-thread » — documenté.*
- **Discipline de lock cohérente** : chaque mutateur GUI tracé prend le même mutex récursif (preview/import/AssignSample, teardown projet wrappé `AppWindow.cpp:522-523`). `SetMixBus` early-out + cache `busIndex_` pour éviter le churn Remove/Insert temps-réel (`PlayerChannel.cpp:61-87`).
- **FTZ (flush-to-zero) posé correctement sur les DEUX threads de rendu** avec rationale exacte (`AudioOutDriver.cpp:47-62` ; `AudioMixer.cpp:22-36`) — **le fix A53 le plus important**, fait juste, par-thread.
- **Enregistrement de stems hors thread audio** : SPSC lock-free drainé par thread de fond, fences `__sync_synchronize()` correctement placés, politique drop-rather-than-block avec compteur loggé (`WavFileWriter.cpp`).
- **Teardown du thread audio** : vrai `SDL_WaitThread` (pas de délai deviné), sémaphore non-cappé. Save atomique (temp+rename) et reset canal qui clear `instr_` ferment des UAF/corruptions antérieures.

**DSP** — les bons instincts perf sont déjà là :
- Boucle interne **pur Q16.15** ; `fp_mul` se réduit à un `smull+asr` AArch64. Pas de float/div/sinf/pow dans le cœur par-sample quand EQ/comp/LFO sont off.
- Transcendantaux comp/EQ/LFO/ADSR **hoistés au prologue par-bloc** ; compresseur via **table de gain 256 entrées** au lieu de `powf` par sample. EQ/comp/delay en float normalisé avec `satF2Fp` saturant (évite le clic de wrap INT32).
- Toutes les features sont gated par flag : **coût zéro quand off**. Scratch/mixBuffer réutilisés (pas de malloc par bloc en régime établi).

**Chargement & parsing** :
- **Parser WAV nettement plus robuste qu'upstream** : skip de chunks pré-fmt/pré-data arbitraires (JUNK/LIST/bext/fact) avec padding word-align, `WAVE_FORMAT_EXTENSIBLE` résolu via GUID, support 8/16/24/32-bit int + float, gardes 64 itérations contre les boucles infinies. Charge des fichiers réels que l'ancien code rejetait.
- Down-convert 24/32/float **correct en little-endian**, clamp float propre. `GetBuffer` tolère défensivement l'échec d'alloc (null-checks). `Swap16/32` no-op sur cette cible LE (zéro overhead). `PurgeSample` new/free corrigé en `SAFE_FREE`.
- **La primitive de streaming incrémental borné existe et fonctionne** (`WavFile::GetBuffer(start,count)` + `AudioFileStreamer`) — base réelle pour la future voix « tape ».

**Migrations & save** :
- Expansion canaux **8→16 propre et cohérente** via `SONG_CHANNEL_COUNT` partout (zéro « 8 » résiduel dans player/mixer/instrument) ; `MAX_BUS_COUNT = SONG_CHANNEL_COUNT + 1` symbolique. Re-stride de migration gated sur le byte count legacy exact, parcouru haut→bas (pas de clobber).
- Fix truncation-corruption réel (temp+rename, `::remove` avant `fopen('w')`) ; fix buffer `+1`/NUL avant `TiXmlDocument::Parse`. Patches récents **inhabituellement bien documentés sur le POURQUOI** (FTZ, save atomique, sizing SPSC) — capture de savoir tribal de qualité.

---

### Notes de confiance / désaccords entre agents
- **Faible confiance / latent** : P4, P5, P8, P11, le null-deref `EnableRendering` et l'overflow offset 32-bit sont des bugs **latents** (équilibrés/masqués aujourd'hui par l'ordre de teardown ou des call-sites balancés), pas des crashs observés. À traiter comme du durcissement, pas comme des incendies — mais bon marché.
- **Convergence forte** : 3 agents corrigent indépendamment le commentaire « pool allocator » obsolète et recommandent des lectures plus grosses ; 2 agents signalent `SAMPLELOADCHUNKSIZE` et la double-copie/4 Ko. Haute confiance sur ces points.
- **Contradiction explicite avec la note projet** : le build n'utilise **pas** `-ffast-math`/`-funroll-loops`/`-mcpu=cortex-a53` (vérifié dans `Makefile.PORTMASTER`). Plusieurs raisonnements DSP supposaient ces flags actifs — P3 est donc un préalable, pas un complément.
- **Nuance importante sur « longs samples »** : le vrai plafond est ~6 min (position float), pas la RAM ni l'overflow entier. Symptôme **subtil** (wobble pitch/timing), facile à mal diagnostiquer — d'où sa priorité P6 malgré une sévérité « High » et non « Critical ».