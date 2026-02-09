# Roadmap D-Engine (Windows-only)

Ce document suit ce qui est déjà fonctionnel et ce qu’il reste à réaliser.
La priorité est la qualité du SDK Contract (API/ABI), la performance, et le respect strict de la philosophie du moteur.

## Décisions produit (fixes)
- Plateformes : Windows uniquement.
- Priorité : SDK Contract (contrats, ABI, compatibilité, documentation).
- Renderer : non décidé ; objectif = « meilleure implémentation possible » même si complexe.
- Outils : non prioritaires tant que le core contract est solide.
- Deadline : aucune, qualité avant vitesse.

## Philosophie technique (rappel)
- Header-first, contrats explicites, coût visible.
- Zéro exceptions et zéro RTTI dans le Core.
- Allocateurs D-Engine uniquement, pas de `new/delete` bruts.
- Déterminisme et ordering stables par défaut.
- Performance-first, pas d’allocations cachées en hot path.

## Déjà fonctionnel (réalisé)
- [x] Build MSVC via `D-Engine.sln` + cibles Debug/Release.
- [x] Smokes runtime : `AllSmokes` + `ModuleSmoke`.
- [x] Policy lint (base + strict + modules) et self-test.
- [x] Contrats + Null backends M0 (Window/Renderer/Time/Jobs/Input/FileSystem).
- [x] ABI/Interop (ModuleLoader + headers ABI).
- [x] Politiques déterminisme et threading documentées.
- [x] BenchRunner + baselines CI perf.

## Roadmap par phases

### Phase 0 — Stabilisation du SDK Contract (terminée)
Objectif : rendre les contrats et l’ABI solides, documentés et testés.
- [x] Audit complet des contrats publics (Purpose/Contract/Notes, ownership, determinism flags).
- [x] Checklist de compatibilité ABI (versioning, alignments, tailles, invariants).
- [x] Tests compile-only pour chaque header public manquant.
- [x] Couverture smoke minimale pour chaque Null backend.
- [x] Policy lint strict sans allowlists résiduelles.
- [x] Clarifier la règle « header-first » (fichiers `detail/`, extern templates).
- [x] Validation des règles de threading/déterminisme dans les contrats système.

## Audit des contrats (Phase 0)

### Synthèse
- Tous les contrats M0 sont cohérents : types POD, v-tables dynamiques, concepts statiques.
- Les blocs `Purpose/Contract/Notes` au niveau fichier sont présents et bien alignés avec la philosophie.
- Manques identifiés : indicateurs explicites de déterminisme et de thread-safety dans chaque contrat.

### Points par contrat
- `Source/Core/Contracts/Window.hpp`
  - OK : `WindowDesc`, `WindowEvent`, handles POD, status enum.
  - À ajouter : flags explicites `Determinism` + `ThreadSafetyNotes` (contract-level).
- `Source/Core/Contracts/Time.hpp`
  - OK : `TimeCaps`, `FrameTime`, API minimaliste.
  - À ajouter : champs pour mode Replay/Strict + note sur origine du temps.
- `Source/Core/Contracts/Jobs.hpp`
  - OK : handles, counters, fallback séquentiel.
  - À ajouter : clauses de déterminisme (ordering) + mode Replay.
- `Source/Core/Contracts/Input.hpp`
  - OK : événements simples, interface poll.
  - À ajouter : règles d’ordering des events + thread-safety explicite.
- `Source/Core/Contracts/FileSystem.hpp`
  - OK : PathView, API sans alloc.
  - À ajouter : règles sur caching/déterminisme + thread-safety explicite.
- `Source/Core/Contracts/Renderer.hpp`
  - OK : handles, caps, frame submission, interface statique/dynamique.
  - À ajouter : déterminisme (frameIndex), ordering sur SubmitInstances, thread-safety.

### Phase 1 — Core Runtime solide (v0.2)
Objectif : init/lifecycle cohérents et runtime stable.
- [ ] Orchestration core (init, shutdown, config globale).
- [ ] Memory system complet (tracking, OOM policy, reporting standardisé).
- [ ] Job system deterministe (mode Replay) + instrumentation.
- [ ] ABI/Interop stabilisés (DLL Windows, signatures fixes).

### Phase 2 — Rendering & Windowing Windows (v0.3)
Objectif : renderer Windows-ready sans sacrifier la qualité.
- [ ] Décision renderer (DX12 recommandé mais non décidé).
- [ ] Windowing Win32 + swapchain + input basique.
- [ ] Ressources GPU (buffers, textures, descriptors) + shaders HLSL.
- [ ] Capabilities reportées proprement (formats, MSAA, features).

### Phase 3 — Assets & IO (v0.4)
Objectif : pipeline d’assets minimal, déterministe et fiable.
- [ ] FileSystem/IO Windows + caching simple.
- [ ] Format d’assets minimal (metadata + blobs).
- [ ] Hashing stable pour replays et build incrémental.
- [ ] CLI d’import/export (si nécessaire).

### Phase 4 — Gameplay minimal (v0.5)
Objectif : boucle de jeu simple et déterministe.
- [ ] ECS/Scene minimal + scheduling deterministe.
- [ ] Input mapping + actions.
- [ ] Audio/Physics basiques (ou stubs contractuels).

### Phase 5 — SDK 1.0 (Windows)
Objectif : API/ABI stables et documentés, prêts pour intégration.
- [ ] Contrats gelés + compatibilité ABI versionnée.
- [ ] Packaging SDK (headers, libs, modules null).
- [ ] Samples Windows (headless + windowed).
- [ ] Benchmarks reproductibles + critères de perf.

## Suivi des actions
- Ce fichier est la source unique de la roadmap.
- Toute tâche terminée doit être marquée `[x]`.
- Toute nouvelle priorité doit être ajoutée dans la phase correspondante.
