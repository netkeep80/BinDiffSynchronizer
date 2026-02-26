# ТЗ: jhub — Унифицированная JSON-инфраструктура для разработки ПО

**Статус:** Актуален
**Версия:** 1.1
**Дата:** 2026-02-26
**Связанные issue:** [#36](https://github.com/netkeep80/BinDiffSynchronizer/issues/36), [#38](https://github.com/netkeep80/BinDiffSynchronizer/issues/38)

---

## 1. Введение и контекст

### 1.1 Мотивация

Современные платформы разработки (GitHub, GitLab) — это «комбайны», объединяющие репозитории, CI/CD, реестры образов и систему управления задачами. Однако их архитектура строится вокруг **Git-репозиториев с файловой системой** и **Docker-контейнерами** как единицами развёртывания.

С приходом AI-агентов и разработки по требованиям (requirements-driven development) возникает необходимость в новом формате хранения требований, кода и инфраструктуры — **JSON как универсальном языке представления данных и программ**.

Данный документ описывает ТЗ (Техническое Задание) на разработку **jhub** — платформы разработки следующего поколения, построенной на единой JSON-инфраструктуре.

### 1.2 Определения

| Термин | Определение |
|--------|-------------|
| **jgit** | Темпоральная JSON база данных с моделью версионирования Git; реализована в данном репозитории (Фазы 1–3) |
| **jdb** | JSON Database — мультидокументная база данных на основе jgit с исполняемыми JSON-документами через [jsonRVM](https://github.com/netkeep80/jsonRVM) |
| **jhub** | Платформа разработки, объединяющая jdb, jgit, Docker Swarm, CI/CD и реестр образов в единую JSON-инфраструктуру |
| **jsonRVM** | JSON Runtime Virtual Machine — движок исполнения JSON-документов как программ |
| **json-commit** | Единица версии в jgit: снимок JSON-дерева с метаданными (автор, время, родительский коммит) |
| **json-repo** | Репозиторий jgit — версионируемый JSON-документ с историей изменений |

### 1.3 Связь с существующей реализацией

Данный проект реализует базовые примитивы для jhub:

| Компонент | Статус | Описание |
|-----------|--------|----------|
| `ObjectStore` | ✓ Фаза 1 | Content-addressed хранение JSON в CBOR формате |
| `PersistentJsonStore` | ✓ Фазы 2–3 | Zero-parse хранилище JSON-дерева |
| `persistent_string/map/array` | ✓ Фаза 3 | Тривиально-копируемые JSON-примитивы |
| `persistent_json` | ✓ Фаза 3 | nlohmann::basic_json с persistent_string |
| `BinDiffSynchronizer` | ✓ Оригинал | Бинарная дифференциальная синхронизация |
| `jgit::Repository` | Фаза 4 | Система коммитов, веток, checkout |
| **jdb** | Фаза 5 | Мультидокументная JSON БД |
| **jhub** | Фаза 6 | Полная платформа разработки |

---

## 2. Требования к jhub

### 2.1 Функциональные требования

#### FR-1: Управление JSON-репозиториями (jgit integration)

| ID | Требование | Приоритет |
|----|-----------|-----------|
| FR-1.1 | jhub хранит репозитории как jgit-репозитории (JSON-документы с историей версий) | Высокий |
| FR-1.2 | Поддержка операций: `clone`, `push`, `pull`, `fork` для jgit-репозиториев | Высокий |
| FR-1.3 | Просмотр истории коммитов, diff между версиями через Web GUI | Высокий |
| FR-1.4 | Ветки и теги в jgit-репозиториях | Средний |
| FR-1.5 | Pull Request (Merge Request) как JSON-документ с историей обсуждения | Средний |
| FR-1.6 | Issue Tracker как коллекция JSON-документов в jdb | Средний |
| FR-1.7 | Децентрализованное хранение: репозитории можно синхронизировать P2P через jgit pull/push | Низкий |

#### FR-2: Исполняемые JSON-требования (jdb + jsonRVM)

| ID | Требование | Приоритет |
|----|-----------|-----------|
| FR-2.1 | Требования к ПО хранятся как JSON-документы в jdb | Высокий |
| FR-2.2 | Требования исполняются через jsonRVM для верификации и тестирования | Высокий |
| FR-2.3 | Трассировка: каждое требование связано с кодом (json-commit), тестом и CI-результатом | Высокий |
| FR-2.4 | Требования версионируются в jgit (полная история изменений требований) | Средний |
| FR-2.5 | AI-агенты получают доступ к требованиям через REST API (JSON-формат) | Средний |
| FR-2.6 | Генерация кода/тестов на основе JSON-требований через AI-агентов | Низкий |

#### FR-3: CI/CD Pipeline как JSON

| ID | Требование | Приоритет |
|----|-----------|-----------|
| FR-3.1 | CI/CD пайплайны хранятся как JSON-документы в jdb, исполняемые через jsonRVM | Высокий |
| FR-3.2 | Запуск пайплайна при push в jgit-репозиторий (аналог GitHub Actions) | Высокий |
| FR-3.3 | Runner: агент, выполняющий JSON-шаги пайплайна (аналог GitLab Runner) | Высокий |
| FR-3.4 | Артефакты пайплайна хранятся в jdb как JSON-блобы | Средний |
| FR-3.5 | Статус пайплайна виден в Web GUI | Средний |
| FR-3.6 | Параллельное выполнение шагов пайплайна | Средний |
| FR-3.7 | Кэширование зависимостей между запусками пайплайна | Низкий |

#### FR-4: Docker-интеграция

| ID | Требование | Приоритет |
|----|-----------|-----------|
| FR-4.1 | Реестр Docker-образов встроен в jhub (аналог Docker Hub) | Высокий |
| FR-4.2 | Метаданные образов (теги, слои, зависимости) хранятся в jdb | Высокий |
| FR-4.3 | Развёртывание через Docker Swarm описывается JSON-документами в jdb | Высокий |
| FR-4.4 | Автоматическое развёртывание при успешном прохождении CI (JSON pipeline + Swarm) | Средний |
| FR-4.5 | Откат развёртывания через checkout предыдущего jgit-коммита инфраструктурного репо | Средний |
| FR-4.6 | Мониторинг состояния Swarm-кластера как JSON-документ в jdb (live state) | Низкий |

#### FR-5: Web GUI

| ID | Требование | Приоритет |
|----|-----------|-----------|
| FR-5.1 | Web GUI реализуется как JSON-приложение на jsonRVM (self-hosting) | Высокий |
| FR-5.2 | Просмотр jgit-репозиториев, коммитов, diff | Высокий |
| FR-5.3 | Редактор JSON-требований с трассировкой | Средний |
| FR-5.4 | Визуализация CI/CD пайплайнов | Средний |
| FR-5.5 | Dashboard состояния инфраструктуры (Docker Swarm, репозитории, пайплайны) | Средний |

#### FR-6: Безопасность и контроль доступа

| ID | Требование | Приоритет |
|----|-----------|-----------|
| FR-6.1 | Аутентификация через JSON Web Token (JWT) | Высокий |
| FR-6.2 | Авторизация: права доступа к репозиториям хранятся как JSON-документы в jdb | Высокий |
| FR-6.3 | Подпись коммитов: каждый json-commit подписывается ключом автора | Средний |
| FR-6.4 | Аудит-лог всех операций в jdb | Средний |

### 2.2 Нефункциональные требования

| ID | Требование | Метрика |
|----|-----------|---------|
| NFR-1 | **Производительность**: чтение jgit-репозитория без парсинга JSON при old state | Открытие ≤ 100 мс для репо с 10 000 коммитов |
| NFR-2 | **Масштабируемость**: поддержка горизонтального масштабирования через Docker Swarm | До 100 узлов кластера |
| NFR-3 | **Надёжность**: персистное хранилище jdb с WAL (Write-Ahead Log) | RPO ≤ 1 секунда |
| NFR-4 | **Совместимость**: jgit использует собственную модель версионирования и **не совместим с форматом Git-репозитория**; импорт стандартных Git-репо не предусмотрен | — |
| NFR-5 | **Кросс-платформенность**: Linux, macOS, Windows | Все три платформы |
| NFR-6 | **Отказоустойчивость**: репликация jdb между узлами через jgit push/pull | Нет единой точки отказа |

---

## 3. Архитектура jhub

### 3.1 Обзор архитектуры

```
╔══════════════════════════════════════════════════════════════╗
║                         jhub Platform                        ║
╠══════════════════════════════════════════════════════════════╣
║  ┌─────────────────────────────────────────────────────────┐ ║
║  │                    Web GUI Layer                         │ ║
║  │  (JSON-приложение на jsonRVM, SPA-подобный интерфейс)   │ ║
║  └────────────────────────┬────────────────────────────────┘ ║
║                           │ REST API (JSON)                   ║
║  ┌─────────────────────────────────────────────────────────┐ ║
║  │                   jhub API Server                        │ ║
║  │  (C++ или Go сервер, экспортирующий REST API для jdb)   │ ║
║  └──────────┬──────────────┬──────────────┬────────────────┘ ║
║             │              │              │                   ║
║  ┌──────────▼──┐  ┌────────▼────┐  ┌─────▼──────────────┐   ║
║  │  jgit Layer │  │  jdb Layer  │  │  Docker Layer       │   ║
║  │  Repos      │  │  Issues     │  │  Registry (образы)  │   ║
║  │  Commits    │  │  PRs        │  │  Swarm (деплой)     │   ║
║  │  Branches   │  │  Pipelines  │  │  Services (мониторинг)│  ║
║  │  Diffs      │  │  Users/ACL  │  │                    │   ║
║  └──────────┬──┘  └────────┬────┘  └─────┬──────────────┘   ║
║             │              │              │                   ║
║  ┌──────────▼──────────────▼──────────────▼──────────────┐   ║
║  │            Persistent Storage Layer                     │   ║
║  │  ObjectStore  │  PersistentJsonStore  │  PageDevice    │   ║
║  │  (CBOR blobs) │  (zero-parse pools)   │  (page cache)  │   ║
║  └───────────────────────────────────────────────────────┘   ║
╚══════════════════════════════════════════════════════════════╝
```

### 3.2 Компонентная архитектура

#### Компонент 1: jgit — Версионирование JSON

Фундамент jhub — уже реализованная система jgit (Фазы 1–3 данного репозитория).

```
jgit Repository (.jgit/)
├── HEAD                         ← текущая ветка
├── refs/
│   ├── heads/                   ← ветки (json-файлы с hash)
│   └── tags/                    ← теги
├── objects/
│   ├── <2hex>/
│   │   └── <62hex>              ← CBOR-encoded json-blob
│   └── ...
├── pools/
│   ├── values.bin               ← PersistentJsonStore value_pool (mmap-able)
│   ├── arrays.bin               ← array_pool
│   └── objects.bin              ← object_pool
└── index                        ← рабочее дерево (staged changes)
```

**Типы объектов jgit:**

| Тип | Структура | Хранение |
|-----|-----------|---------|
| `json-blob` | Бинарное представление JSON-узла | CBOR в ObjectStore |
| `json-tree` | `{key: child_hash, ...}` — дочерние узлы | CBOR в ObjectStore |
| `json-commit` | `{tree, parent, author, timestamp, message}` | CBOR в ObjectStore |
| `json-ref` | `$ref` — ссылка на объект в другом репо | fptr в PersistentJsonStore |

**JSON-структура json-commit:**
```json
{
  "type": "json-commit",
  "tree": "sha256:abcd1234...",
  "parent": "sha256:ef567890...",
  "author": {
    "name": "Alice",
    "email": "alice@example.com",
    "timestamp": 1735689600
  },
  "message": "Add user authentication module",
  "requirements": ["req:auth-001", "req:auth-002"]
}
```

**Сетевой протокол jgit:**

Для передачи данных между узлами jgit (push/pull) используется **HTTP/2 + REST**. Это обеспечивает:
- Совместимость с браузерами (в т.ч. с Web GUI на WebAssembly)
- Стандартную аутентификацию через JWT (`Authorization: Bearer <token>`)
- Мультиплексирование запросов и server push через HTTP/2

#### Компонент 2: jdb — JSON Database

jdb строится поверх jgit и предоставляет мультидокументную базу данных:

```
jdb (JSON Database)
├── Каждый документ = jgit-репозиторий
├── Документы адресуются по имени (аналог таблицы/коллекции)
├── Запросы: JSON Pointer, JSON Path
├── Транзакции: атомарный коммит нескольких документов
└── Репликация: push/pull между узлами jdb
```

**Пример структуры jdb-хранилища:**
```
jdb/
├── repositories/
│   ├── myproject.git/           ← код проекта (jgit-репо)
│   └── config.git/              ← конфигурация (jgit-репо)
├── issues/
│   ├── 001.git/                 ← issue #1
│   └── 002.git/                 ← issue #2
├── pull_requests/
│   └── 001.git/                 ← PR #1
├── pipelines/
│   └── ci.json                  ← JSON CI/CD пайплайн
├── users/
│   └── alice.git/               ← профиль пользователя
└── registry/
    └── myapp/
        └── 1.0.0/               ← Docker image metadata
```

**Исполнение JSON через jsonRVM:**
```json
{
  "type": "json-pipeline",
  "name": "build-and-test",
  "steps": [
    {
      "name": "build",
      "$exec": "docker build -t myapp:${version} .",
      "requires": ["checkout"]
    },
    {
      "name": "test",
      "$exec": "docker run myapp:${version} npm test",
      "requires": ["build"]
    },
    {
      "name": "push",
      "$exec": "docker push myapp:${version}",
      "requires": ["test"],
      "condition": "${branch} == 'main'"
    }
  ]
}
```

#### Компонент 3: jhub API Server

Сервер, предоставляющий REST API для управления всеми компонентами:

```
jhub API Server
├── GET  /api/repos                         ← список репозиториев
├── POST /api/repos                         ← создать репо
├── GET  /api/repos/{name}/commits          ← история коммитов
├── GET  /api/repos/{name}/diff/{a}/{b}     ← diff между коммитами
├── POST /api/repos/{name}/push             ← push изменений
├── GET  /api/repos/{name}/pull/{branch}    ← pull обновлений
├── GET  /api/issues                        ← список issues
├── POST /api/issues                        ← создать issue
├── GET  /api/pipelines/{name}/runs         ← история запусков CI
├── POST /api/pipelines/{name}/trigger      ← запустить pipeline
├── GET  /api/registry/images               ← список Docker образов
├── POST /api/registry/push                 ← загрузить образ
├── GET  /api/swarm/services                ← состояние Swarm
└── POST /api/swarm/deploy                  ← развернуть сервис
```

**Технологический стек API Server:**
- Язык: **Go** (основной кандидат для Cloud Native стека и REST API) или **Rust** (для компонентов с требованиями к безопасности памяти и производительности); требуется дальнейший анализ — см. [migration-analysis.md](https://github.com/netkeep80/jsonRVM/blob/master/migration-analysis.md)
- Протокол: **HTTP/2 + REST** (для совместимости и поддержки браузерных клиентов, в том числе Web GUI на WebAssembly)
- Формат: JSON (все запросы/ответы)
- Аутентификация: JWT в заголовке `Authorization: Bearer <token>`

#### Компонент 4: Docker Integration

```
jhub Docker Layer
├── Registry Service
│   ├── Получает образы через Docker Registry API v2
│   ├── Метаданные слоёв хранятся в jdb
│   └── Блобы слоёв хранятся в ObjectStore (content-addressed)
├── Swarm Manager
│   ├── Читает deployment-конфиги из jdb
│   ├── Применяет через Docker Swarm API
│   └── Статус сервисов пишет обратно в jdb
└── CI Runner
    ├── Слушает события push в jgit-репозитории
    ├── Запускает JSON-пайплайн через jsonRVM
    └── Docker-шаги выполняются в изолированных контейнерах
```

#### Компонент 5: Web GUI

```
jhub Web GUI
├── SPA-приложение, описанное JSON-документами в jdb
├── Рендеринг через jsonRVM (JSON-шаблоны → HTML)
├── Компоненты:
│   ├── RepoView: просмотр jgit-репозитория
│   ├── CommitGraph: граф коммитов
│   ├── DiffViewer: JSON Patch визуализация
│   ├── IssueTracker: создание и просмотр issues
│   ├── PipelineView: статус CI/CD
│   ├── RegistryView: Docker образы
│   └── SwarmDashboard: состояние кластера
└── Self-hosting: jhub GUI является jgit-репозиторием в самом jhub
```

### 3.3 Архитектура развёртывания

```
Docker Swarm Cluster
╔════════════════════════════════════════════════════════════╗
║  Node 1 (Manager)          Node 2 (Worker)  Node N        ║
║  ┌────────────────┐        ┌───────────┐    ┌───────────┐ ║
║  │ jhub-api       │        │ jhub-api  │    │ jhub-api  │ ║
║  │ (replicated)   │        │           │    │           │ ║
║  ├────────────────┤        ├───────────┤    ├───────────┤ ║
║  │ jdb-storage    │        │ jdb-mirror│    │ jdb-mirror│ ║
║  │ (primary)      │        │ (replica) │    │ (replica) │ ║
║  ├────────────────┤        └───────────┘    └───────────┘ ║
║  │ registry       │                                        ║
║  │ (объектное хра-│                                        ║
║  │  нилище образов│                                        ║
║  └────────────────┘                                        ║
╚════════════════════════════════════════════════════════════╝
       ↑ push/pull         ↑                 ↑
   jhub client         jhub client       jhub client
```

**Docker Compose / Swarm deployment.json:**
```json
{
  "type": "jhub-deployment",
  "version": "1.0",
  "services": {
    "jhub-api": {
      "image": "jhub/api:latest",
      "replicas": 3,
      "ports": ["8080:8080"],
      "volumes": ["/data/jdb:/app/jdb"],
      "environment": {
        "JDB_PATH": "/app/jdb",
        "JWT_SECRET": "${secrets.jwt_secret}"
      }
    },
    "jhub-registry": {
      "image": "jhub/registry:latest",
      "replicas": 1,
      "ports": ["5000:5000"],
      "volumes": ["/data/registry:/registry"]
    }
  }
}
```

---

## 4. Планирование разработки jhub

### 4.1 Фаза 4: jgit Repository Layer (jgit complete)

**Цель:** Реализовать полноценную систему коммитов и веток поверх Фаз 1–3.

| Задача | Описание | Зависимость |
|--------|----------|-------------|
| 4.1 | Структура `json-commit` (RFC в jgit/commit.h) | Фаза 3 |
| 4.2 | Операции `commit()`, `checkout()`, `log()` | 4.1 |
| 4.3 | Ветки: `branch create/switch/list` | 4.2 |
| 4.4 | Теги: `tag create/list` | 4.2 |
| 4.5 | Diff: JSON Patch между двумя коммитами | 4.2 |
| 4.6 | Merge: трёхстороннее слияние через JSON Merge Patch | 4.3 |
| 4.7 | CLI: `jgit init`, `jgit commit`, `jgit log`, `jgit diff`, `jgit checkout` | 4.2–4.6 |
| 4.8 | Тесты для всех операций | 4.1–4.7 |

**Примечание:** Структура `Repository` и `Commit` уже объявлена в `jgit/repository.h` и `jgit/commit.h`.

### 4.2 Фаза 5: jdb Layer

**Цель:** Реализовать мультидокументную JSON БД поверх jgit.

| Задача | Описание | Зависимость |
|--------|----------|-------------|
| 5.1 | `jdb::Database` — коллекция jgit-репозиториев | Фаза 4 |
| 5.2 | `jdb::Collection` — именованная коллекция документов | 5.1 |
| 5.3 | `jdb::Document` — отдельный JSON-документ в jdb | 5.2 |
| 5.4 | Запросы: JSON Pointer, фильтрация, проекция; **B-tree индексы по JSON-полям для оптимизации queries** | 5.3 |
| 5.5 | Транзакции: атомарный коммит нескольких документов | 5.3 |
| 5.6 | Репликация: push/pull между узлами jdb | 5.5, Фаза 4 |
| 5.7 | Интеграция с jsonRVM для исполняемых JSON-документов | 5.6 |
| 5.8 | WAL и crash recovery | 5.5 |
| 5.9 | REST API для jdb (embedded сервер) | 5.4–5.7 |
| 5.10 | Тесты и бенчмарки | 5.1–5.9 |

### 4.3 Фаза 6: jhub Layer

**Цель:** Реализовать полноценную платформу разработки поверх jdb.

#### Фаза 6.1: jhub API Server

| Задача | Описание |
|--------|----------|
| 6.1.1 | jhub API Server — Go или Rust HTTP/2-сервер (HTTP/2 + REST, см. Раздел 7) |
| 6.1.2 | Репозитории API (`/api/repos`) |
| 6.1.3 | Issues API (`/api/issues`) |
| 6.1.4 | Pull Requests API (`/api/pull_requests`) |
| 6.1.5 | Аутентификация JWT |
| 6.1.6 | Авторизация (ACL в jdb) |

#### Фаза 6.2: CI/CD Pipeline

| Задача | Описание |
|--------|----------|
| 6.2.1 | JSON Pipeline DSL (формат пайплайна) |
| 6.2.2 | jsonRVM интеграция для исполнения шагов |
| 6.2.3 | jhub Runner — агент выполнения пайплайнов |
| 6.2.4 | Хук: запуск пайплайна при push в jgit |
| 6.2.5 | Параллельное выполнение шагов |
| 6.2.6 | API управления пайплайнами (`/api/pipelines`) |

#### Фаза 6.3: Docker Integration

| Задача | Описание |
|--------|----------|
| 6.3.1 | Docker Registry API v2 (push/pull образов) |
| 6.3.2 | Хранение метаданных образов в jdb |
| 6.3.3 | Docker Swarm Manager интеграция |
| 6.3.4 | Deployment JSON DSL |
| 6.3.5 | API развёртывания (`/api/swarm`) |

#### Фаза 6.4: Web GUI

| Задача | Описание |
|--------|----------|
| 6.4.1 | JSON UI DSL (формат компонентов) |
| 6.4.2 | jsonRVM rendering pipeline |
| 6.4.3 | RepoView, CommitGraph, DiffViewer |
| 6.4.4 | IssueTracker, PRView |
| 6.4.5 | PipelineView, RegistryView, SwarmDashboard |
| 6.4.6 | Authentication UI |

---

## 5. Технические решения и обоснование

### 5.1 Почему всё хранится в JSON?

1. **Единый язык**: JSON понятен людям, AI-агентам, и машинам одновременно.
2. **Исполнимость**: через jsonRVM JSON-документы становятся исполняемыми программами.
3. **Версионируемость**: любой JSON-документ можно версионировать через jgit.
4. **Трассируемость**: связь требование → код → тест → деплой выражается ссылками `$ref` между JSON-документами.
5. **Децентрализованность**: jgit позволяет синхронизировать части jdb через P2P.

### 5.2 Отличие от GitHub/GitLab

| Критерий | GitHub/GitLab | jhub |
|----------|--------------|------|
| Репозиторий | Файловая система + Git | JSON-дерево + jgit |
| Требования | Markdown-текст в Issues | Исполняемые JSON-документы |
| CI/CD | YAML-пайплайны | JSON-пайплайны, исполняемые через jsonRVM |
| Реестр образов | Docker Hub (внешний) | Встроенный, метаданные в jdb |
| Инфраструктура | K8s/Docker Compose | Docker Swarm, конфиги в jdb |
| Хранилище | SQL + объектное хранилище | jdb (jgit-based) |
| Персистность | Непрозрачная | Прозрачная (JSON-документы) |
| Децентрализованность | Ограничена | Нативная (P2P jgit) |
| AI-интеграция | Внешние плагины | Нативная (требования + API) |

### 5.3 Хранение бинарных файлов в jdb

В `persistent_json` добавляется поддержка типа `binary` — по аналогии с `nlohmann::json`. Это позволяет хранить произвольные бинарные данные (`.so`, `.exe`, `.jpg`, Docker-слои и т.д.) непосредственно в JSON-документах jdb.

**Схема работы:**

| Аспект | Решение |
|--------|---------|
| Ссылка в JSON | `$ref` или `$bin` — указатель на бинарный объект в хранилище |
| Хранение | ObjectStore — content-addressed CBOR-блоб (по SHA-256 хешу содержимого) |
| Экспорт | При экспорте JSON: ссылка на jgit-объект (если получатель понимает jgit) **или** inline base64-кодирование |
| Дедупликация | Автоматическая через content-addressing: одинаковые бинарники хранятся один раз |

**Пример JSON-документа с бинарным полем:**
```json
{
  "name": "myapp",
  "version": "1.0.0",
  "executable": {
    "$bin": "sha256:abc123...",
    "size": 1048576,
    "mime": "application/octet-stream"
  },
  "icon": {
    "$bin": "sha256:def456...",
    "mime": "image/png"
  }
}
```

### 5.5 Использование BinDiffSynchronizer

`BinDiffSynchronizer<persistent_json_value>` обеспечивает **репликацию jdb** между узлами:

```
Node A (primary)              Node B (replica)
┌──────────────────┐          ┌──────────────────┐
│ persistent_json  │          │ persistent_json  │
│ value_pool       │          │ value_pool       │
│ ┌──────────────┐ │  delta   │ ┌──────────────┐ │
│ │ [v1][v2][v3] │ │ ──────→  │ │ [v1][v2][v3] │ │
│ └──────────────┘ │          │ └──────────────┘ │
└──────────────────┘          └──────────────────┘
   BinDiffSynchronizer вычисляет delta между pool-снимками
   Передаётся только изменившееся (оптимизация трафика)
```

### 5.6 AI-агенты и jhub

jhub создаёт идеальную среду для AI-агентов:

```
AI Agent Workflow
┌─────────────────────────────────────┐
│  1. Чтение требований (jdb)          │
│     GET /api/collections/requirements│
│                                     │
│  2. Генерация кода (jgit commit)     │
│     POST /api/repos/myproject/push   │
│                                     │
│  3. Запуск CI (json pipeline)        │
│     POST /api/pipelines/build/trigger│
│                                     │
│  4. Проверка результатов             │
│     GET /api/pipelines/build/runs/1  │
│                                     │
│  5. Создание PR если тесты прошли   │
│     POST /api/pull_requests          │
└─────────────────────────────────────┘
Каждый шаг — JSON-документ с полной трассировкой
```

---

## 6. Связь компонентов

### 6.1 Диаграмма зависимостей

```
jsonRVM ←──── jdb ←──── jhub
  ↑              ↑          ↑
  │              │          │
  └── jgit ──────┘          │
        ↑                   │
        │                   │
  BinDiffSynchronizer        │
  persist<T>/fptr<T>         │
  ObjectStore                │
  PersistentJsonStore        │
  (Фазы 1-3 ✓)             │
                             │
  Docker Swarm ──────────────┘
  Docker Registry
```

### 6.2 Поток данных: push + CI

```
Developer push
     │
     ▼
jgit receive-pack  ──→  jgit commit stored in ObjectStore
     │
     ▼
jhub-api: push hook triggered
     │
     ▼
jdb: create pipeline run document
     │
     ▼
jhub-runner: reads JSON pipeline from jdb
     │
     ├──▶ Step 1: docker build  ──→  jhub-registry: store image
     │         (jsonRVM exec)
     ├──▶ Step 2: docker test   ──→  jdb: store test results
     │         (jsonRVM exec)
     └──▶ Step 3: swarm deploy  ──→  Docker Swarm: create/update service
              (if main branch)       jdb: update deployment state
```

---

## 7. Принятые решения по открытым вопросам

Ниже приведены ответы на вопросы, которые ранее оставались открытыми.

| Вопрос | Решение | Обоснование |
|--------|---------|-------------|
| Язык API Server | **Go, Rust или WebAssembly** (требуется дальнейший анализ) | Go оптимален для API и Cloud Native стека; Rust — для критичных по производительности компонентов; WASM — для браузерного GUI. Полный анализ: [migration-analysis.md](https://github.com/netkeep80/jsonRVM/blob/master/migration-analysis.md) |
| jsonRVM зрелость | **Требует доработки** для подготовки к jhub | Текущая C++ реализация имеет критические проблемы (Windows-only DLL, кодировка CP1251, отсутствие CI). Рекомендуется: краткосрочно — исправить C++ версию, среднесрочно — реализовать на Go. Детали: [migration-analysis.md](https://github.com/netkeep80/jsonRVM/blob/master/migration-analysis.md) |
| Совместимость с Git | **Нет** | jgit использует собственную модель версионирования JSON-документов и не совместим с форматом Git-репозитория. Импорт стандартных Git-репо не предусмотрен. |
| Хранение бинарных файлов | **Расширение `binary` в персистном хранилище** | В `persistent_json` добавляется тип `binary` по аналогии с `nlohmann::json`. Ссылки через `$ref` или `$bin`. При экспорте: ссылка на jgit-объект либо конвертирование в base64. Блобы хранятся в ObjectStore (content-addressed). |
| Индексирование jdb | **Да, для queries** | B-tree индексы по JSON-полям реализуются в Фазе 5.4 для поддержки JSON Pointer / JSON Path запросов. |
| Сетевой протокол jgit | **HTTP/2 + REST** | Выбран для совместимости и работы в браузерах (в том числе для Web GUI на jsonRVM/WASM). |

---

## 8. Критерии приёмки jhub (MVP)

Минимально рабочая версия jhub считается готовой, когда:

1. ✓ **jgit complete**: `init`, `commit`, `checkout`, `log`, `diff`, `branch`, `merge` работают через CLI
2. ✓ **jdb basic**: создание/чтение/обновление JSON-документов с историей версий
3. ✓ **jhub API**: REST API для repos, issues, pipelines
4. ✓ **CI Runner**: запуск JSON-пайплайна при push в репозиторий
5. ✓ **Docker Registry**: push/pull Docker-образов
6. ✓ **Swarm Deploy**: развёртывание сервиса через JSON-конфиг
7. ✓ **Web GUI**: просмотр репозиториев, коммитов, issues в браузере
8. ✓ **Self-hosting**: jhub сам хранится и развёртывается через jhub

---

## 9. Ссылки

- [BinDiffSynchronizer](https://github.com/netkeep80/BinDiffSynchronizer) — текущий репозиторий
- [jsonRVM](https://github.com/netkeep80/jsonRVM) — движок исполнения JSON-программ
- [nlohmann/json](https://github.com/nlohmann/json) — JSON для C++: JSON Patch, Pointer, CBOR
- [RFC 6902: JSON Patch](https://datatracker.ietf.org/doc/html/rfc6902) — формат дельт JSON
- [RFC 6901: JSON Pointer](https://datatracker.ietf.org/doc/html/rfc6901) — адресация узлов JSON
- [RFC 7396: JSON Merge Patch](https://datatracker.ietf.org/doc/html/rfc7396) — слияние JSON
- [Docker Registry API v2](https://distribution.github.io/distribution/spec/api/) — API реестра Docker
- [Docker Swarm](https://docs.docker.com/engine/swarm/) — Docker оркестрация
- [analysis.md](analysis.md) — анализ BinDiffSynchronizer
- [plan.md](plan.md) — план развития проекта
- [persistent_json_analysis.md](persistent_json_analysis.md) — анализ применимости persistent_json для jgit/jdb

---

*Документ создан: 2026-02-26. Авторы: AI Issue Solver (konard/BinDiffSynchronizer#37, #39), на основе требований netkeep80/BinDiffSynchronizer#36, #38.*
*Версия 1.1: уточнены язык API Server (Go/Rust/WASM), зрелость jsonRVM, совместимость с Git (отсутствует), хранение бинарных файлов ($bin/$ref + ObjectStore), индексирование jdb (B-tree, Фаза 5.4), сетевой протокол jgit (HTTP/2 + REST).*
