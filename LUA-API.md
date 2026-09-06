# CrossPoint Lua Mini-App API (`cp`)

CrossPoint Reader의 한국어 포크(`release/korean-lua`)에 추가된 **Lua 미니앱 시스템**의 공식 스펙입니다.

SD 카드의 `.lua` 스크립트는 파일 탐색기에서 선택하면 `LuaRunnerActivity`가 실행합니다. 스크립트는 전역 테이블 `cp`(CrossPoint)에 노출된 호스트 API를 호출하여 화면을 그리고, 입력을 받고, SD 파일을 읽고 쓸 수 있습니다.

## 런타임 개요

- **인터프리터**: Lua 5.4.6 (`lib/Lua`, MIT 라이선스), `LUA_32BITS` 활성(ESP32-C3 RV32IMC 최적화)
- **활동**: `LuaRunnerActivity` (`src/activities/util/`), VM은 활동 진입 시 생성, 종료 시 파괴 → 모든 VM 메모리는 힙에 transient, 종료 시 전부 반환
- **실행 모델**: 스크립트는 렌더 태스크 안에서 1회 실행됩니다. 최초 렌더 시 메인 청크가 실행되고, 그 안에서 `cp.gfx.display()`를 호출할 때까지 화면에 커밋되지 않습니다.
- **허용 Stdlib**: `base`, `math`, `string`, `table`, `utf8`, `coroutine` 일부. `io`/`os`/`debug`/`package`/`loadfile`/`dofile`은 **포함되지 않음** — 파일 접근은 반드시 `cp.file`을 통해서만.
- **메모리 한계**: 스크립트 파일 최대 ~32KB, `cp.file` 1회 64KB.

## 공통 동작

### 메모리 안전성
- 모든 그리기/파일 호출은 내부적으로 HAL 뮤텍스(`HalStorage`, `RenderLock`)를 경유하므로 스레드 안전.
- `cp.gfx` 함수는 **렌더 태스크**(`render()`)에서만 실행됨 — 스크립트는 실행 동안 그리기 전용임을 보장.

### 홈 복귀 (범용 탈출구)
Back + Power 버튼을 **동시에** 누르면 어떤 스크립트가 무엇을 하든 홈으로 돌아갑니다. Lua Runner 레벨에서 처리되므로 스크립트가 `back`/`power` 핸들러를 등록해도 무시하고 동작합니다.

---

## `cp.gfx` — 화면 그리기

렌더러(`GfxRenderer`)에 대한 얇은 바인딩입니다. 모든 좌표는 **논리 화면 좌표**(px)이며, 논리 크기는 `cp.gfx.size()`로 얻습니다.

화면 방향(오리엔테이션)에 따라 논리 크기가 달라집니다:
- **Portrait**: 480 × 800
- **Landscape**: 800 × 480

```lua
cp.gfx.clear()
cp.gfx.text("Hello", 10, 10)
cp.gfx.textwidth("Hello")          -- 정확한 픽셀 폭
cp.gfx.rect(x, y, w, h, false)     -- 테두리 사각형
cp.gfx.fill(x, y, w, h)            -- 채운 사각형
cp.gfx.line(x1, y1, x2, y2)
cp.gfx.w, cp.gfx.h = cp.gfx.size() -- 논리 화면 크기
cp.gfx.display()                   -- 프레임버퍼를 화면(e-ink)에 커밋
```

### `cp.gfx.clear()`
프레임버퍼 전체를 지웁니다 (흰색).

### `cp.gfx.text(text, x, y [, opts])`
텍스트를 그립니다. `x,y`는 **왼쪽 기준선** 좌표입니다 (글리프가 위로 솟음).

| opts 필드 | 타입 | 설명 | 기본 |
|---|---|---|---|
| `font` | string | 폰트 선택 (아래 표) | `"ui"` |
| `style` | string | `"regular"` \| `"bold"` \| `"italic"` \| `"bold_italic"` | `"regular"` |
| `color` | string | `"black"` \| `"white"` (반전) | `"black"` |

**사용 가능한 폰트** (펌웨어에 실제 등록된 것만):

| 이름 | 실제 폰트 | 비고 |
|---|---|---|
| `"ui"`, `"pretendard"` | Pretendard 10pt | 기본 UI 폰트 |
| `"kopub"`, `"reader"` | KoPub Batang 14pt | 한국어 독자 폰트 |
| `"system"`, `"custom"` | SD 시스템/커스텀 폰트 | 설정으로 로드 시 |
| `"sans12"`…`"sans18"`, `"serif12"`…`"serif18"` | (ID 미등록) | **현재 사용 불가** — 빌드에 데이터 없음 |

> **참고**: `sans16`, `serif18` 등은 `fontIds.h`에 ID만 있고 실제 폰트 데이터가 펌웨어에 포함되어 있지 않아 그리지 않습니다. 사용 가능한 것은 위 표의 실제 등록 폰트뿐입니다.

### `cp.gfx.textwidth(text [, opts])`
`opts`(`font`, `style`)를 반영한 **정확한 픽셀 폭**을 반환합니다. 우측 정렬·중앙 정렬 계산에 쓰세요.

### `cp.gfx.digits(text, x, y, scale [, opts])`
**도트 매트릭스 숫자**(7-segment풍 3x5 글리프)를 임의 배율로 그립니다. 큰 숫자(시계·타이머·점수)를 폰트 크기 제약 없이 (flash 추가 없이) 그릴 때 사용합니다.

- `scale`: 픽셀 배율 (1 cell = scale×scale px). 기본 2.
- 지원 문자: `0-9`, `:`(콜론), `.`, `-`, 공백. 그 외 문자는 작은 간격으로 스킵.
- `opts.color = "white"`로 반전.

```lua
local tw = cp.gfx.digits_width("12:34", 6)
cp.gfx.digits("12:34", math.floor((W - tw) / 2), 40, 6)  -- 중앙 큰 시계
```

### `cp.gfx.digits_width(text, scale)`
`cp.gfx.digits`로 그릴 때의 정확한 픽셀 폭 반환 (정렬 계산용).

### `cp.gfx.rect(x, y, w, h, filled)`
사각형. `filled=false`면 테두리, `true`면 채움(=`fill`). 마지막 인자 생략 시 `true`.

### `cp.gfx.fill(x, y, w, h)`
채워진 사각형.

### `cp.gfx.line(x1, y1, x2, y2)`
선.

### `cp.gfx.size()`
`width, height` 두 값을 반환: `local w, h = cp.gfx.size()`

### `cp.gfx.display()`
프레임버퍼를 디스플레이에 커밋합니다. 스크립트 끝에서 반드시 호출해야 화면이 갱신됩니다.

---

## `cp.input` — 입력

버튼/스와이프 이벤트를 스크립트에 전달합니다. 핸들러는 전역 `_cp_input` 테이블에 저장되어 VM 파괴 시 자동 해제됩니다.

```lua
cp.input.on("confirm", function(key) ... end)         -- 릴리즈
cp.input.on_press("confirm", function() ... end)      -- 프레스
cp.input.swipe(function(dir) ... end)                 -- "left"|"right"|"up"|"down"
local held = cp.input.pressed("confirm")              -- 폴링 (bool)
```

### `cp.input.on(key, handler [, edge])`
`key`가 **릴리즈**될 때 `handler(key)`를 호출합니다. `edge`를 `"pressed"`로 주면 프레스 이벤트에 바인딩합니다.

### `cp.input.on_press(key, handler)`
`cp.input.on(key, handler, "pressed")`와 동일.

### `cp.input.swipe(handler)`
스와이프 시 `handler(direction)` 호출. `direction`: `"left"`, `"right"`, `"up"`, `"down"`.

### `cp.input.pressed(key)`
그 버튼이 **현재 눌려있는지** bool 반환 (프레임 폴링).

### 인식 키

| key | 버튼 |
|---|---|
| `"confirm"` | 확인(Enter) |
| `"back"` | 뒤로(Esc) |
| `"up"` / `"down"` / `"left"` / `"right"` | 방향키 |
| `"pageback"` / `"pageforward"` | 사이드 페이지 버튼 |
| `"power"` | 전원(P) |

### Back 버튼 특수 규칙
- 스크립트가 `cp.input.on("back", ...)`을 **등록하지 않으면**, Back(Esc)은 파일 브라우저 복귀로 동작합니다.
- 등록하면 **그 스크립트가 Back을 소비**합니다 (예: 계산기의 백스페이스).
- Back+Power 동시는 항상 홈 복귀 — 이건 스크립트와 무관하게 동작합니다.

---

## `cp.file` — SD 카드 파일 I/O

스크립트가 접근할 수 있는 **유일한** 파일 인터페이스입니다. `io`/`os` 라이브러리가 없으므로 모든 파일 조작은 이 모듈로 합니다.

모든 경로는 **SD 루트 기준 절대 경로**(`/`로 시작)여야 하며, `..` 성분은 거부됩니다. 내부적으로 `HalStorage` 뮤텍스를 경유하므로 SdFat 스레드 안전성이 유지됩니다.

**크기 한계**: 1회 read/write에 64KB까지. 초과 시 오류 반환.

```lua
local s = cp.file.read("/books/data.txt")   -- 문자열, 실패 시 nil + err
local ok = cp.file.write("/logs/a.txt", "hi")  -- 덮어쓰기
local ok = cp.file.append("/logs/a.txt", "line\n")
local items = cp.file.list("/books")         -- 테이블 (숨김 제외)
local ok = cp.file.exists("/a.txt")
local isdir = cp.file.isdir("/books")
local ok = cp.file.remove("/a.txt")
local ok = cp.file.mkdir("/newdir")
```

### `cp.file.read(path)`
파일 전체를 문자열로. 실패 시 `nil, err` 2개 반환.

### `cp.file.write(path, data)`
파일 전체를 덮어씁니다. `true`/`false` (+실패 시 err).

### `cp.file.append(path, data)`
파일 끝에 추가.

### `cp.file.list(path)`
디렉터리 안의 이름(파일/폴더) 배열. 숨김(`.` 시작) 항목 제외. 실패 시 `nil, err`.

### `cp.file.exists(path)`, `cp.file.isdir(path)`
`bool`.

### `cp.file.remove(path)`, `cp.file.mkdir(path)`
`bool`.

---

## `cp.time` — 시각 · 경과시간 (읽기 전용)

RTC 및 부팅 경과시간을 읽습니다. **블로킹 sleep은 없습니다** — 스크립트는 렌더 태스크에서 실행되므로, 잠자면 UI가 멈춥니다. 지연/주기 갱신은 `cp.frame`을 사용하세요.

```lua
local ok = cp.time.available()          -- RTC 존재 여부 (bool)
local h, m = cp.time.gettime()          -- 시, 분 (RTC 없으면 nil, nil)
local now = cp.time.now()               -- 날짜/시간 테이블 (아래), 없으면 nil
local ms = cp.time.millis()             -- 부팅 후 경과 ms (uint32 랩 어라운드 주의)
```

### `cp.time.available()`
`true`/`false`. X4/X4 Pro/X3 모두 RTC가 있어 보통 `true`.

### `cp.time.gettime()`
현재 시각의 시(0-23), 분(0-59) 두 값을 반환. RTC가 없거나 읽기 실패 시 `nil, nil`.

### `cp.time.now()`
한 번에 반환되는 테이블:

| 필드 | 타입 | 의미 |
|---|---|---|
| `year` | number | 4자리 연도 |
| `month` | number | 1-12 |
| `day` | number | 1-31 |
| `hour` | number | 0-23 |
| `minute` | number | 0-59 |
| `second` | number | 0-59 |
| `weekday` | number | 0=일요일 .. 6=토요일 |

RTC가 없거나 읽기 실패 시 `nil` 반환.

### `cp.time.millis()`
부팅 후 경과 밀리초. `uint32`(약 49일) 랩어라운드가 있으므로 **차이(`now - start`)로** 쓰세요. `cp.frame`의 타이밍 또는 경과시간 측정에 유용합니다.

---

## `cp.frame(intervalMs, handler)` — 주기 갱신

시계·타이머·애니메이션·게임 루프용 비동기 주기 틱입니다. 블로킹하지 않고, 설정된 간격마다 렌더 태스크에서 핸들러를 호출합니다.

```lua
-- 1초마다 handler 실행 (시계)
cp.frame(1000, function()
  local h, m = cp.time.gettime()
  if h then
    cp.gfx.clear()
    cp.gfx.text(string.format("%02d:%02d", h, m), 20, 40)
    cp.gfx.display()
  end
end)

-- 중단
cp.frame(nil)
```

### 동작
- **첫 렌더**에서 main 청크가 1회 실행됩니다 (초기화 + 첫 그림).
- 이후 `intervalMs`마다 **handler만** 다시 실행됩니다. main은 재실행되지 않으므로 전역 변수는 유지됩니다.
- handler는 렌더 태스크 안에서 실행되므로 `cp.gfx.*`를 자유롭게 쓸 수 있고, `cp.gfx.display()`로 커밋해야 화면이 갱신됩니다.
- 최소 간격 50ms (e-ink 새로고침 평균 고려). 그 아래로 지정하면 50으로 상한.
- `intervalMs`는 `cp.time.millis()` 흐름을 기준으로 상대 스케줄 — 정밀 실시간 타이밍 보장은 아님.

### 주의
- 핸들러가 오래 걸리면 다음 틱이 밀립니다 (싱글 스레드 + 렌더 태스크).
- 화면을 바꾸는 게 아니면 매 틱마다 `display()`하지 말 것 — e-ink 리프레시는 느립니다.

---

## 실행 규약 & 제약

### 실행 흐름
1. 파일 브라우저에서 `.lua` 선택 → `LuaRunnerActivity` 시작
2. 스크립트 파일 컴파일 (32KB 한계)
3. 최초 렌더 시 메인 청크 실행 (렌더 락 안에서)
4. `cp.input` 핸들러는 이후 입력 루프에서 호출
5. Back 단독 = (등록 시) 스크립트 처리 / (미등록 시) 브라우저 복귀
6. Back+Power = 홈 복귀

### 스크립트 작성 시 주의
- **전역 변수/함수를 사용하는 로직**: Lua에서 `local` 선언보다 앞의 코드는 그 이름을 전역으로 해석합니다. 함수 `A`가 뒤에 정의된 `local function B`를 호출하면 `B`가 nil인 채 실행됩니다. **호출 순서를 생각해 함수 정의를 먼저 넣거나 `local B` forward 선언**을 하세요.
- **정수 좌표**: 그리기 좌표/크기 인자는 정수여야 합니다. 나눗셈 후 `math.floor`로 정수화하세요.
- **긴 문자열 게으른 처리**: `string.format("%.6g", ...)` 등으로 출력 폭을 제한하세요.

### 금지/미지원
- `io.open`, `os.*`, `loadfile`, `dofile`, `package` — 미포함 (컴파일 제외)
- GPIO 직접 접근, 네트워크, FreeRTOS 태스크 — API 미노출
- 네이티브 속도 보장 X (인터프리터)

---

## 디버깅

- **오류 표시**: 스크립트 실행/핸들러 오류는 화면에 표시되고 `LOG_ERR("LUA", ...)`로 로그(`[ERR] [LUA] ...`)됩니다.
- **시뮬레이터**: `pio run -e simulator -t run_simulator`. SD 루트 = 프로젝트 `fs_/`, `.lua`는 `fs_/books/`에 두고 파일 브라우저에서 실행.

## 구현 위치

| 모듈 | 파일 |
|---|---|
| `cp.file` | `lib/Lua/src/LuaRuntime.cpp` (`registerHostApis`) |
| `cp.gfx`, `cp.input`, `cp.time`, `cp.frame` | `src/activities/util/LuaRunnerActivity.cpp` |
| Lua 5.4.6 vendor | `lib/Lua/src/` (`library.json` srcFilter로 stdlib 제한) |
| 활동/디스패치 | `src/activities/util/LuaRunnerActivity.cpp/.h` |
| RTC 접근 | `lib/hal/HalClock.cpp/.h` (`getDateTime`) |
| 확장자 인식 | `lib/FsHelpers/FsHelpers.cpp/.h` |