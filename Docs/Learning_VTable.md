# Comprendre la "Magie" cachée du C++ : V-Table et Smoke Tests

> Ce document a été écrit pour aider les contributeurs débutants à comprendre des concepts clés utilisés dans le D-Engine.

## 1. La V-Table (Virtual Method Table)

### Le Problème
Dans le code du moteur, vous verrez souvent des interfaces comme `IAllocator` :

```cpp
// Source/Core/Memory/Allocator.hpp
class IAllocator {
public:
    virtual void* Allocate(usize size, usize alignment) = 0;
    // ...
};
```

Quand le moteur appelle `monAllocateur->Allocate(10, 4)`, comment sait-il quel code exécuter ? `monAllocateur` pourrait être un `PageAllocator`, un `MallocAllocator` ou autre chose. Le compilateur ne peut pas le savoir à l'avance !

### La Solution : Le "Menu de Restaurant"
Le C++ utilise un système caché appelé **V-Table**.

Imaginez que `IAllocator` est le concept de **"Restaurant"**.
- Vous entrez dans un bâtiment (l'objet en mémoire).
- Vous ne savez pas si C'est une Pizzeria ou un Sushi Bar.
- Mais à l'entrée, il y a **toujours** un panneau (le **vptr**) qui pointe vers le **Menu** (la **V-Table**).

![Schéma V-Table](https://mermaid.ink/img/pako:eNqNVE1vwjAM_SvIp00C7bBLD2zTCG3H0Q6TEqfNmkR1kiIF_O8rpS0fBQKXJv6e7ee852S9kIwLohfW8qpWgkXsgK3F2nB9y3F9mS9W2RxvZotVdr24WdxmN0uOqxgXjAmeU64Z5UJLqjljlArGgH-0VjAuvCgZ1_B94Xle8J9_T6jgkmtBqeCS1oJj8H-d6B_d6K_dWw_6p7H6p6H6S3vE82Xv8zvi-bS94_my9_k98fxoe8fzZe_ze-L53vaO5xceJ8eR49hx7Dh2HOf24-j9OOO8c55LzqnnmHPuOOece045px5jznPHuefcc8o59Zh_wXk-B94W3kbeHrxveJ8D7wveN7y3gfcD7wfejwPvJ95PvJ8F3k-8n3g_C7yfeD_xfhZ4P_F-4v0s8H7i_cT7WfzFvJtVq02xmK_m69tVfrXIs9Vmdfu72mR5dpXP8_w2X9xm2Wqz-s2lUj9Qvj6D?type=png)

### Mécaniquement
1. **L'Objet (`monAllocateur`)** contient un petit pointeur caché (`vptr`) tout au début.
2. Ce `vptr` pointe vers un tableau statique en mémoire (la **V-Table** de la classe réelle, ex: `PageAllocator`).
3. Ce tableau contient les adresses réelles des fonctions (`PageAllocator::Allocate`, etc.).
4. L'appel devient : *"Va voir le menu, regarde la ligne 1, et exécute la fonction indiquée"*.

### Pourquoi c'est important ?
Cela permet le **Polymorphisme** : écrire du code générique (comme `ChargerTexture(IAllocator*)`) qui fonctionnera avec n'importe quel futur allocateur qu'on inventera, sans avoir à réécrire la fonction `ChargerTexture`.

---

## 2. Les Smoke Tests

### Concept
L'expression vient de l'électronique : la première chose qu'on fait en réparant un appareil, c'est de le brancher. Si de la fumée sort, c'est fini, on arrête tout.

### Dans D-Engine
Ce sont des tests situés dans `tests/smoke/`. Ils sont :
1. **Très simples** : Vérifient juste que 1 + 1 = 2.
2. **Critiques** : S'ils échouent, le moteur est fondamentalement cassé.
3. **Rapides** : Ils s'exécutent en une fraction de seconde au démarrage.

Exemple (`tests/smoke/Math_smoke.cpp`) :
```cpp
// Si cette addition ne marche pas, inutile de lancer la physique du jeu !
Vec3f c = a + b;
DNG_ASSERT(c.x == 5.0f ...);
```

> **Règle d'or** : Ne jamais commenter un Smoke Test qui échoue. Corrigez le code !
