# 🎛️ Plan d'intégration — Module VST/console desktop pour LGPT

> But : greffer le **host VST/AU JUCE déjà éprouvé** (du fork m8c de Romain, `~/Desktop/M8C`) sur **LGPT desktop**, pour en faire une **console live hybride** — les 16 pistes samples de LGPT + ~4 canaux "aux" (entrée carte son **ou** VST), le tout dans un mixer commun (inserts + sends + master), synchro au transport LGPT, avec MIDI in.
>
> **Contrainte absolue : compatibilité 1:1 avec la version Trimui.** Le module est **desktop-only** (ajouté au build desktop), le cœur LGPT (séquenceur, samples, format de projet) reste **identique**. Un projet ouvert sur Trimui ignore tout le module et joue les pistes sèches.

---

## 0. Ce qui est DÉJÀ fait (à recycler, pas à réécrire)

Le fork m8c contient un **host JUCE complet, façade `extern "C"` propre** (`M8C/src/host/juce_host.h` + `juce_host.mm`). Il fait déjà :
- Topologie de bus : N inserts piste + 3 FX returns + 3 sends + master, chacun une chaîne ordonnée de plugins. (`JUCE_HOST_NUM_TRACKS` paramétrable → on le passera de 8 à **16+aux**.)
- **Nourri en AUDIO** : `juce_host_process(in, channels, frames, outStereo)` (+ `_process_full` pour les wets). C'est exactement ce qu'il nous faut : on lui donne des stems, il rend la stéréo.
- **MIDI** : `juce_host_push_midi`, `juce_host_bus_set_midi_channel` (lane → VST instrument), `juce_host_transport`/`juce_host_clock`/`juce_host_bpm` (sync tempo/transport).
- Sends per-track atomiques, **PDC** (compensation de latence plugin), persistance JSON (`juce_host_save/load`), scanner out-of-process, éditeurs natifs, MIDI-learn + 3 quick params/slot.
- Doc d'archi détaillée : `M8C/docs/vst-host-architecture.md`, `per-output-inserts.md`.

**Le moteur DSP/host est résolu.** Le travail LGPT = la **colle** (build, feed audio, routing MIDI, UI, persistance). Pas de DSP à écrire.

La UI `M8C/src/plugin_rack.c` est en **SDL3** + style overlay m8c → c'est la seule grosse partie à **réécrire** pour le framework de vues de LGPT (SDL2). Le moteur (`juce_host`) est indépendant de SDL.

---

## 1. Principes d'intégration

1. **Desktop-only, frontière C.** Le host JUCE = lib statique C++17 compilée à part (CMake, sa recette existe), linkée dans le build desktop de LGPT **via la façade `extern "C"`** (`juce_host.h` ne contient que des handles opaques + POD). Le mismatch de standard (LGPT mac = gnu++14, Trimui = gnu++03, JUCE = C++17) disparaît à la frontière C. **Le build Trimui ne voit JAMAIS JUCE.**
2. **Cœur intact.** Le module se branche à **UN** point (la sommation bus→master), derrière un `#ifdef` desktop. Le séquenceur, les samples, le format song/pattern/instrument : zéro changement.
3. **Projet portable + sidecar desktop.** L'état du rack (`juce_host_save`) est écrit dans un **fichier sidecar** à côté du projet (ex. `<projet>/pluginrack.json`). Trimui ne le lit pas → joue les pistes sèches. Aucune modif du format de projet LGPT.
4. **Réutiliser le host tel quel.** On n'optimise/réécrit pas le host ; on l'alimente.

---

## 2. Architecture — flux de signal (desktop)

```
   [Séquenceur LGPT]  ── tempo/transport/clock ──►  juce_host_transport()/_clock()/_bpm
        │
   [AudioMixer : 16 bus rendus dans childScratch_[i] (Q16 stéréo)]
        │
   [+ ~4 canaux AUX]
        ├─ source = entrée carte son (capture duplex)  ──► stem aux
        └─ source = VST instrument (lane MIDI)          ──► (rendu PAR le host via push_midi)
        │
        ▼  (DESKTOP ONLY, dans AudioOutDriver::Trigger, AVANT clipToMix)
   convert Q16 → float, assembler les stems (16 pistes + aux)
        │
        ▼
   juce_host_process(stems_float, nCh, frames, outStereo_float)
        │  (inserts par bus → sends → FX returns → master, + PDC)
        ▼
   outStereo_float → (RTAudio float, ou → SINT16) → sortie
```

- **Point d'insertion exact** : `sources/Services/Audio/AudioOutDriver.cpp::Trigger()`. Aujourd'hui : `AudioMixer::Render(primarySoundBuffer_)` somme les 16 bus → `clipToMix()` (Q16→short) → `driver_->AddBuffer`. **Desktop** : après le rendu des bus (les `childScratch_[i]` de `AudioMixer`), au lieu de la somme master, on passe les bus comme **stems** au host JUCE, et sa stéréo de sortie remplace `primarySoundBuffer_`. **Trimui** : chemin actuel inchangé (`#ifndef` / runtime flag).
- **Accès aux 16 bus** : `AudioMixer` rend déjà chaque bus dans `childScratch_[i]` (la zone qu'on connaît, cf. le fix multicœur). Il faut exposer ces scratch au point d'insertion (les sommer plus tard, pas dans `sumChildrenParallel`). Détail à régler : soit le host est branché à la place de la sommation master, soit on récupère les scratch après rendu.
- **Format** : LGPT = Q16 (`fixed`), RTAudio Mac = SINT16 (`sources/Adapters/RTAudio/RTAudioDriver.cpp` : `openStream(&params, NULL, RTAUDIO_SINT16, …)`). JUCE = float [-1,1]. → conversions Q16→float (entrée host) et float→SINT16 (sortie). Cheap, au boundary uniquement.

---

## 3. Les 2 seams durs (le vrai risque — tout le reste se lift)

### Seam A — Build (CMake/JUCE ⟷ Makefile/LGPT)
- LGPT desktop = `projects/Makefile.MACOS` (clang, gnu++14, SDL2, RTAudio). JUCE = CMake + FetchContent + C++17.
- **Parade** : compiler `juce_host.mm` + JUCE en **lib statique `liblgpt_juce_host.a`** via un petit CMake (lifté de `M8C/CMakeLists.txt` + `cmake/FindJUCE.cmake`), puis **linker la `.a`** dans `Makefile.MACOS` (ajouter le `.a` + les frameworks macOS que JUCE réclame : CoreAudio, Accelerate, etc.). LGPT n'inclut que `juce_host.h` (C).
- Bumper `JUCE_HOST_NUM_TRACKS` 8 → 16, ajouter les lanes aux.
- **Trimui** : `Makefile.PORTMASTER` ne touche à rien (pas de JUCE, le code d'insertion sous `#ifdef LGPT_VSTHOST`).

### Seam B — Run-loop macOS (JUCE MessageManager ⟷ boucle LGPT bloquante)
- JUCE doit être **pompé** régulièrement (fenêtres d'éditeurs de plugins, scan, timers). LGPT a une boucle **bloquante** : `SDLEventManager::MainLoop()` → `SDL_WaitEvent` (`sources/Adapters/SDL2/GUI/SDLEventManager.cpp:77`).
- **Parade** : sur desktop, remplacer `SDL_WaitEvent` par `SDL_WaitEventTimeout(&e, ~8ms)` (ou `SDL_PollEvent` + petit sleep) et appeler `juce_host_pump()` à chaque tour. Garde la réactivité UI, pompe JUCE ~120Hz. (m8c a résolu ça via le pump dans `SDL_AppIterate`.) **Desktop-only** ; Trimui garde `SDL_WaitEvent` bloquant (économie batterie).
- `juce_host_init()` au boot desktop (après init SDL), `juce_host_shutdown()` au quit.

**Si P0 valide A+B sans crash, le plus gros est derrière.**

---

## 4. Roadmap séquencée (de-risking, transposée de la doc m8c)

| Phase | Scope | Critère d'acceptation |
|---|---|---|
| **P0 — Seam build + run-loop** | CMake → `liblgpt_juce_host.a` (FetchContent JUCE), linkée dans `Makefile.MACOS`. `juce_host_init/pump/shutdown` câblés ; `SDL_WaitEventTimeout` + `juce_host_pump()` dans MainLoop. **Zéro audio.** | LGPT desktop build + run avec JUCE linké ; fenêtre SDL2 OK ; pas de conflit NSApp ; quit propre. **Valide le risque n°1 avant toute feature.** |
| **P1 — 1 plugin, master, sans UI** | `juce_host_prepare(sr, maxBlock)` au démarrage audio ; dans `AudioOutDriver::Trigger` (desktop), après le rendu : Q16→float du mix master → `juce_host_process` (1 VST hardcodé sur le master) → float→SINT16 → sortie. | Son du master traité par un VST, sans glitch ; save/restore d'état du plugin OK. |
| **P2 — 16 stems + inserts piste** | Passer les 16 bus (`childScratch_[i]`) comme stems ; chaîne insert par piste + master. | Chaque piste a sa chaîne d'inserts ; somme propre au master ; plusieurs plugins en série. |
| **P3 — Sends + FX returns** | 3 sends + 3 FX returns (modfx/delay/reverb), gains de send par piste. | Chaque send traite sa somme, retours mixés au master. |
| **P4 — Transport / sync** | Driver `juce_host_transport`/`_clock`/`_bpm` depuis le séquenceur LGPT (`SyncMaster` / VAR_TEMPO). | Plugins tempo-sync (delays, LFO) calés sur le tempo LGPT ; start/stop suivis. |
| **P5 — Canaux AUX + duplex audio** | Ouvrir l'entrée carte son (RTAudio duplex : input params au lieu de `NULL`), capturer N canaux → stems aux → mixer. Source par aux = audio-in **ou** VST instrument (lane MIDI). | Un synthé branché en entrée passe dans le mixer + FX + master, en live, latence jouable. |
| **P6 — MIDI in** | Router MIDI in (RTMidi / `MidiService`) → `juce_host_push_midi` ; per-lane MIDI channel pour jouer les VST instruments des aux. | Un clavier joue un VST instrument d'un canal aux, synchro, en live. |
| **P7 — UI rack** | Vue LGPT (pas l'overlay SDL3 m8c) : grille de bus, load/bypass/reorder, params, quick-params/MIDI-learn. Ou portage minimal de `plugin_rack`. | Ouvrir le rack, charger/réordonner/bypass live, régler params, persiste. |
| **P8 — Éditeurs natifs** | `juce_host_slot_open_editor` → fenêtres GUI plugin natives (déjà géré par le host). | Clic slot → éditeur s'ouvre, édite, ferme proprement. |
| **P9 — Persistance + compat** | Sidecar `pluginrack.json` à côté du projet ; load auto sur desktop, ignoré sur Trimui. | Projet ouvert sur Trimui = pistes sèches, aucun artefact ; sur desktop = rack restauré. |

**Latence live (P5)** : pour jammer *à travers* le mixer, le buffer doit être petit (round-trip in→FX→out). Ça pilotera `AUDIOBUFFERSIZE` desktop. Pour du matériel séquencé, non critique. Note : l'audio-in externe se synchronise en **suivant le MIDI clock de LGPT** (clock out), LGPT ne "cale" pas un flux audio entrant.

---

## 5. Compatibilité Trimui (garanties)

- Tout le code d'insertion + le module sous `#ifdef LGPT_VSTHOST` (défini seulement par le build desktop). `Makefile.PORTMASTER` ne définit rien, ne link pas JUCE.
- Le format de projet LGPT **n'est pas modifié**. L'état du rack vit dans un **sidecar** (`pluginrack.json`) que Trimui n'ouvre pas.
- Les canaux **aux n'existent pas** côté Trimui (pas d'audio-in / VST) → ils sont purement desktop, dans le sidecar.
- Les 16 pistes natives (samples) restent le **cœur portable** : composées sur Trimui, ouvertes telles quelles sur desktop, puis enrichies par le mixer VST.

---

## 6. Risques & questions ouvertes

**Risques (classés) :**
1. **Seam build + run-loop** (P0) — le make-or-break. Mitigé par le spike P0 sans feature.
2. **Temps réel** : `juce_host_process` sur le thread audio LGPT (buffering/worker) — aucun malloc/lock (le host pré-alloue ; warm-up des plugins hors thread). Cf. les leçons du moteur audio LGPT.
3. **Duplex / latence live** (P5) : ouvrir l'input RTAudio + aligner in/out ; buffer petit pour le jeu live.
4. **Plugin qui crashe** : scanner out-of-process (déjà dans le host) ; un `processBlock` qui plante reste un risque (watchdog plus tard).
5. **Signing macOS** : entitlements `disable-library-validation` + hardened runtime pour charger des VST tiers.

**Questions à trancher avant de commencer :**
- **Q1** Format de sortie desktop : on reste en SINT16 (conversion float→short après le host) ou on passe la sortie RTAudio en float pour éviter une conversion ? (Recommandé : float out sur desktop.)
- **Q2** Nombre de canaux aux : 4 fixes ? Et leur source est-elle figée (1=audio-in, 2=VST…) ou un sélecteur par canal ?
- **Q3** Combien d'entrées carte son simultanées veux-tu capturer (stéréo ×N) ? (Dimensionne le duplex.)
- **Q4** v1 inclut-il les VST **instruments** sur les aux (P5/P6), ou v1 = audio-in + FX/mix seulement, VST instruments en v2 ?
- **Q5** UI : on réécrit un rack natif LGPT, ou on porte l'overlay `plugin_rack` (SDL3→SDL2) ? (Réécrire est sûrement plus propre dans le framework LGPT.)
- **Q6** JUCE pinné à la version d'Element/m8c (8.0.12) pour lifter le PDC à l'identique — OK ?

---

## 7. Carte des fichiers

**Côté LGPT (à toucher, desktop-only sauf indication) :**
- `sources/Services/Audio/AudioOutDriver.cpp` — `Trigger()` : point d'insertion (feed stems → host → sortie). `#ifdef LGPT_VSTHOST`.
- `sources/Services/Audio/AudioMixer.{h,cpp}` — exposer les `childScratch_[i]` (bus) au point d'insertion sans les sommer (desktop).
- `sources/Adapters/RTAudio/RTAudioDriver.cpp` — duplex (input params) pour les aux ; éventuellement float out.
- `sources/Adapters/SDL2/GUI/SDLEventManager.cpp` — `MainLoop()` : `SDL_WaitEventTimeout` + `juce_host_pump()` (desktop).
- `sources/Adapters/MacOS/MacOSMain/MacOSmain.cpp` — `juce_host_init/shutdown`.
- `sources/Services/Midi/MidiService.*` — router MIDI in → `juce_host_push_midi` ; clock/transport.
- `sources/Application/Player/` (SyncMaster / tempo) — driver `juce_host_transport/_clock/_bpm`.
- `projects/Makefile.MACOS` — linker `liblgpt_juce_host.a` + frameworks ; `-DLGPT_VSTHOST`.
- Nouveau `sources/host/` (exclu du build Trimui) — copie de `juce_host.{h,mm}` + le CMake JUCE.
- Nouvelle vue rack (Application/Views) — UI desktop.
- Persistance : sidecar `pluginrack.json` (hook au load/save projet, desktop).

**Côté m8c (à lifter) :** `M8C/src/host/juce_host.{h,mm}`, `M8C/CMakeLists.txt` + `cmake/FindJUCE.cmake`, `M8C/src/backends/m8_audio_capture.c` (capture duplex), `M8C/src/plugin_rack.{h,c}` (modèle UI), docs `vst-host-architecture.md` / `per-output-inserts.md`.

---

## 8. Première action concrète

**Faire P0 et rien d'autre** : sortir `liblgpt_juce_host.a` via CMake/FetchContent (JUCE 8.0.12), la linker dans `Makefile.MACOS`, câbler `juce_host_init/pump/shutdown` + le pump dans la boucle, **sans audio**. Si LGPT desktop build, run, et que SDL2 + JUCE cohabitent proprement → le risque principal est mort, on enchaîne P1.
