# 🎚️ PASSATION — Streaming de sample depuis la SD (mode « tape » façon M8)

> But : lire des samples **directement depuis la carte SD au fil de la lecture**, sans tout charger en RAM,
> comme la voix « tape »/streaming de la Dirtywave M8. Permet des samples **très longs** (plusieurs minutes/heures)
> sans saturer le 1 Go de RAM du Brick.
>
> **État : PAS implémenté. C'est une feature à construire (effort XL).** Mais la primitive de base existe déjà.

---

## 1. La bonne nouvelle : la primitive de streaming EXISTE déjà et fonctionne

Le code de **preview de sample** (menu d'import) streame DÉJÀ depuis la SD, par bloc audio :

`sources/Application/Audio/AudioFileStreamer.cpp` → `AudioFileStreamer::Render(fixed *buffer,int samplecount)` :
- garde un `WavFile *wav_` ouvert + un `int position_` ;
- à chaque bloc audio appelle `wav_->GetBuffer(position_, count)` = **lecture incrémentale bornée depuis le fichier** (pas tout le fichier) ;
- copie/convertit dans le buffer de sortie, fait `position_ += count`, et `Stop()` à la fin.

Et `WavFile::GetBuffer(long start, long size)` (`sources/Application/Instruments/WavFile.cpp:314`) **sait déjà** :
- lire à partir d'un offset arbitraire (`start`) un nombre borné d'échantillons (`size`) ;
- gérer 8/16/24/32-bit int et 32-bit float, mono/stéréo, en down-convertissant vers du 16-bit ;
- lire par petits chunks (~4 Ko, `readBuffer_` réutilisé) sans jamais tout allouer ;
- `filePos_` évite les seek redondants si on lit séquentiellement (déjà optimisé pour le streaming).

**Donc le « moteur » de streaming est déjà là.** Ce qui manque, c'est de le brancher sur une **voix d'instrument** (pas juste la preview) et de le rendre robuste temps-réel.

---

## 2. Ce qui manque / ce qu'il faut construire

### a) Une voix d'instrument en mode streaming
Aujourd'hui une voix `SampleInstrument` joue depuis `renderParams_.sampleBuffer_` = **un buffer RAM entier** (chargé via `SamplePool`). Voir `SampleRenderingParams.h` : `void *sampleBuffer_`, `float position_`, `rendFirst_/rendLoopStart_/rendLoopEnd_` (tous `int`).

Il faut un **mode « streaming »** opt-in par instrument où, au lieu de pointer un buffer RAM complet, la voix tire ses échantillons d'un **ring buffer alimenté depuis la SD** (via `WavFile::GetBuffer`).

### b) Sortir l'I/O disque du thread audio (DÉPEND DE P2 de l'audit)
⚠️ **Piège majeur.** Aujourd'hui `AudioFileStreamer::Render` appelle `GetBuffer` (= **lecture SD = I/O bloquant**) **directement dans `Render`, sur le thread audio**. Pour la preview ça « passe » (un seul stream, pas critique). Pour des **voix de séquenceur en multicœur**, faire des lectures SD dans le thread audio = **underruns/glitches garantis** (la SD a une latence variable, surtout sous charge).

→ Le design correct : un **thread de décodage dédié** remplit un **ring buffer** par voix streaming ; le thread audio ne fait que **lire** dans le ring (jamais d'I/O). C'est exactement l'item **P2 de l'audit** (« préchargement RAM de `AudioFileStreamer` / retirer les I/O du `Render` »). **Le streaming voix réutilise ce travail.**

### c) Position de lecture 64-bit (DÉPEND DE P6 de l'audit)
⚠️ **Plafond actuel.** La position de lecture est un `float` (`renderParams_.position_`) et les offsets sont des `int` 32-bit. Le `float` dégrade le son au-delà de **~6 min** (2^24 frames) ; les `int` débordent vers ~536M frames.
→ Si on streame des samples longs sans corriger ça, **la longueur est re-plafonnée à ~6 min** → l'intérêt du streaming (longs samples) est annulé.
→ Il faut **P6 = position 64-bit** (index frame `int64` + fraction en virgule fixe) AVANT/AVEC le streaming.
- `AudioFileStreamer` lui-même : `position_` est `int` et `GetSize` renvoie `int` → même plafond à élargir en `long`/`int64`.

**Dépendances explicites (rappel audit) : la voix « tape » dépend de P6 (sinon re-capée à 6 min) et réutilise P2 (threading du décodeur).**

---

## 3. Plan d'implémentation proposé (incrémental, testable à chaque étape)

**Étape 0 — pré-requis** : faire **P6** (position 64-bit) et **P2** (décodeur hors thread audio). Sans ça, le streaming voix sera soit glitché (P2), soit plafonné à 6 min (P6).

**Étape 1 — ring buffer + thread décodeur** :
- Créer un `StreamingSampleSource` (ou enrichir `AudioFileStreamer`) : ouvre un `WavFile`, possède un ring buffer (ex. 64-256 Ko de frames), et un **thread producteur** qui appelle `GetBuffer` pour remplir le ring quand il se vide (low-water mark). Réutiliser le pattern thread/sémaphore déjà en place (cf. `MixRenderWorker` dans `AudioMixer.cpp`).
- Le thread audio ne fait que consommer ; si le ring est vide (SD trop lente) → sortir du silence + compter un underrun (log), pas de blocage.

**Étape 2 — brancher sur une voix d'instrument** :
- Ajouter un flag instrument « streaming » (param de `SampleInstrument`, exposé dans l'UI instrument, persisté).
- Dans le `Render` de la voix : si streaming, lire depuis le ring au lieu de `sampleBuffer_`. **v1 = forward + loop simple uniquement** (pas de reverse, pas de retrig exotique au début — on ajoute après).
- Pitch/speed : l'interpolation/`speed_` s'applique sur les frames sorties du ring comme sur un buffer RAM.

**Étape 3 — fallback RAM** :
- Pour les **samples courts**, garder le chemin RAM actuel (plus simple, pas de thread). Le streaming ne s'active que si l'instrument est en mode streaming **ou** si la taille dépasse un seuil. Décider une politique claire (opt-in explicite recommandé pour v1).

**Étape 4 — robustesse multicœur** :
- Plusieurs voix streaming simultanées = plusieurs threads décodeurs OU un pool de décodage. Attention à la contention SD. Mesurer. Possible de borner le nombre de voix streaming concurrentes.

---

## 4. Fichiers concernés (carte du terrain)

| Fichier | Rôle |
|---|---|
| `sources/Application/Audio/AudioFileStreamer.{h,cpp}` | Streaming SD existant (preview). **Modèle de départ + à threader (P2).** |
| `sources/Application/Instruments/WavFile.{h,cpp}` | `GetBuffer(long start,long size)` = lecture incrémentale SD. `GetSize`/`position` en `int` → à élargir (P6). |
| `sources/Application/Instruments/SampleInstrument.cpp` | Rendu des voix. Là où brancher le mode streaming (lire le ring au lieu de `sampleBuffer_`). |
| `sources/Application/Instruments/SampleRenderingParams.h` | `renderParams` : `sampleBuffer_`, `position_` (float), offsets `int`. Cibles de P6. |
| `sources/Application/Instruments/SamplePool.cpp` | Chargement RAM actuel. Le streaming le COURT-CIRCUITE (ne charge pas tout). |
| `sources/Services/Audio/AudioMixer.cpp` | `MixRenderWorker` = pattern thread/sémaphore à réutiliser pour le thread décodeur. |

---

## 5. Pièges / à NE PAS oublier

1. **Jamais d'I/O SD dans le thread audio.** (Cf. §2b — la cause d'underruns.) Le décodage se fait sur un thread à part qui remplit un ring.
2. **P6 d'abord**, sinon les longs samples (le but) sont re-plafonnés à ~6 min par la position `float`.
3. **Mémoire vs streaming** : le vrai plafond actuel des longs samples n'est PAS la RAM, c'est la position `float` (P6). Le streaming sert à éviter de charger des **gros** fichiers en RAM, pas à contourner un manque de RAM imaginaire pour les durées courtes.
4. **v1 minimaliste** : forward + loop simple, fallback RAM pour les courts. Reverse/retrig/granular = plus tard.
5. **Conversion** : `GetBuffer` down-convertit déjà tout en 16-bit. OK pour v1. (Si on veut du 24-bit interne un jour, c'est un autre chantier.)
6. **Contention SD** sous multicœur avec plusieurs voix streaming : à mesurer, possiblement borner le nombre de streams.

---

## 6. Verdict de l'audit (rappel)
- **Faisabilité : élevée.** La primitive existe (`WavFile::GetBuffer` + `AudioFileStreamer`).
- **Effort : XL.** C'est la « capacité phare M8 », classée **« Plus tard »**.
- **Dépendances dures : P6 (position 64-bit) + P2 (décodeur hors thread audio).** Faire ces deux-là d'abord ; le streaming voix vient ensuite et les réutilise.

> Détails complets dans `AUDIT_AUDIO.md` (section 4 « Mémoire & longs samples » + section 5 feuille de route).
