# GAMMA+ ブラッシュアップ作業ログ

## プロジェクト概要
- 3D/2Dモード切替の光と影パズルアクション(DirectX11 + C++)
- 光源(LightSource) → Caster(OBJ) → Receiver(壁) で **ShadowPrism** を動的生成
- 2Dモードのプレイヤーは壁面に投影された影の上を歩ける
- シーン構造: `SCENE_TITLE` → `SCENE_GAME` (`STAGE_SELECT` / `STAGE_1/2/3`) → `SCENE_RESULT`
- 進行: `STAGE1 → STAGE2 → STAGE3` の順番制 (前段クリアでドア出現)、STAGE3クリアで Result 直行

## エンコーディング(順守すべきルール)
- **大半のファイル: CP932 (Shift-JIS) / BOMなし / CRLF**
- 例外: `manager.cpp`, `game.cpp`, `Option.cpp` (途中で再保存され UTF-8 化)、`MakeRect` 等 ASCII のみの新ヘルパ
- 編集時の注意: Editツールは UTF-8 で扱うため CP932 ファイルの日本語コメントを破壊する恐れあり。**バイトレベル編集 (Python `open('rb')` → `replace` → `open('wb')`) を使う**
- 新規行に日本語コメントを追加する場合も同様。**新規コメントは英語で書く**ことで Edit ツールが安全に使える(ASCIIのみ)
- 注: CP932 で扱う Python 文字列に em-dash (—) などの非 ASCII 句読点を混入させない (`C4819` を新規発生させる)

## キーバインド (現状)
| アクション | キーボード | マウス | ゲームパッド |
|---|---|---|---|
| Up/Down/Left/Right | W/A/S/D | | DPAD |
| Jump | Space | | A |
| Dash | LShift | | X |
| Action (ステージ入場・噴水・ポータル) | F | | B |
| Change (3D↔2D 変身) | F | | B |
| **Push/Pull (箱掴み)** | — | **左クリック** | B |
| Reset | R | | Back |
| Menu (ポーズ) | Esc | | Start |
| Light (光源操作 TAB) | Tab | | Y |
| **Title 開始** | — | **左クリック** | B |

**設計バグメモ**: `ActionKey` と `ChangeKey` が両方とも F / B に重複バインド。
Push は左クリックに分離済み。Change と Action は依然 F だが、Change は MODE_3D ↔ MODE_2D の判定で gate されるため実害は最小。

## 現セッションで完了した主要修正

### 1. ステージクリアシステム新規 (`stage_clear_state.cpp/.h`)
**追加内容**:
- セッション中のクリアフラグ (`g_Cleared[STAGE_MAX]`)
- 順番制ロック判定 `StageClearState_IsUnlocked(stage)`
- ディゾルブキュー (ゴール時 enqueue、stage_select 進入時 consume)
- 各ステージ用ディゾルブエフェクト state (timer, sparkle angle/height/speed)
- オールクリア banner (将来再利用用)
- **カメラ シネマティック state machine** (Phase 1: zoom-in 1.0s / Phase 2: hold 1.5s / Phase 3: zoom-out 1.0s)
- `Camera_SeedPlayer3DFromCurrent()` で終了時のプレイヤーカメラへ滑らかに引き渡し
- ロック種別2種: `IsActive` (カメラ占有) / `IsLocked` (入力ロック、predingも含む)
- Game_Initialize で queue → fade完了 (FADE_NONE) を見て自動 Start (遷移中の stale snapshot 回避)

**ドア破壊エフェクト**:
- 黒い霧 puff 40個、サイズ 1.4、半径 2.2、ドア本体 (door.y + 1.2) を中心に球状拡散
- alpha は ease (0→0.85→0)、puff サイズも時間で 1.0→1.6 倍にスウェル
- ドア Y-scale 崩落タイミング: Phase 2 の **0.4〜0.7s** (霧ピーク中に隠れて消える)
- クリア後はずっと `doorScaleY = 0` (`g_Cleared[stage]` を参照して固定)
- **シャドウキューブマップパスからも skip** (Game_Draw 内、`doorScaleY <= 1e-4` または未ロックなら continue) → ドアの影もちゃんと消える

**CLEAR 表示**:
- `asset/texture/UI/clear.png` を billboard で stage marker (easy/mid/hard) の上 (y+3.2) に表示
- 旧 ImGui テキストオーバーレイは廃止
- STAGE 画像 (easy/mid/hard) は **クリア後も alpha 1.0 のまま** (フェードアウトしない)

**配線**:
- `manager.cpp` Initialize/Finalize で `StageClearState_Initialize/Finalize`
- `title.cpp` Initialize で `StageClearState_Reset()` (新規プレイ時クリア)
- `game.cpp` Game_Update で `StageClearEffect_Update / AllClearSequence_Update / StageClearCinematic_Update` を毎フレーム tick
- `game.cpp` Game_Draw billboard ループで `StageClearEffect_GetClearAlpha` 等を参照
- `field.cpp` draw loop で `StageClearCinematic_GetDoorScaleY` を Y スケールに反映
- `player3D.cpp` `Player3D_CheckGoal`: STAGE3 のとき `SCENE_RESULT` 直行、それ以外は STAGE_SELECT 帰還

### 2. 影 (ShadowPrism) 当たり判定の大改修

**旧コード問題点**:
- `ShadowManager::UpdateAllShadows`: `if (rebuildThreshold < 0.1f) rebuildThreshold = 0.1f;` で再構築閾値が強制0.1に → 光源を毎フレーム細かく動かしても rebuild しない (光源操作速度 0.083/f < 0.1)
- `kMaxRebuildsPerFrame = 3` → stage1の seesaw(2部)+box+obj_2 = 4以上の caster 中3つしか更新されない
- `Shadow_Build` が `dominantMapIdx` (= 一番ヒットの多い1 wall タイル) のヒット点しか polygon 構築に使わない → 影が複数 wall タイルを跨ぐと中央 1 タイル分しか collider が出ない (箱影のスクショで判明)
- `Player2DShadow_Collision` の "inside expanded multi-pass clip" がプリズム内側にいるプレイヤーを反復的に押し出し → 着地時の謎の押し戻し
- `ShadowColliderBox.cpp` の `kSampleSkin = 0.05f` 絶対許容が caster 近接時の数値ジッタで wall hit を弾く

**修正**:
- `UpdateAllShadows`:
  - 0.1f クランプ削除 → config.rebuildThreshold (0.01f) をそのまま使用
  - `kMaxRebuildsPerFrame = 3 → 64` で全 caster を同フレーム更新
- `Shadow_Build`:
  - 旧: `if (h.mapIndex == dominantMapIdx)` の単一タイル絞り
  - 新: 同支配的平面 (normal dot ≥ 0.95 かつ平面オフセット差 ≤ 0.10) の全ヒットを basePts に追加 → 複数 wall タイルに跨がる影も正しく polygon 化
- `RaycastToReceiversDirectional` の `kSampleSkin`: `max(0.05, distToSample * 0.10)` の相対値に
- `IsCasterBetweenLightAndWall` の wallSkin も同様に相対化
- **`Player2DShadow_Collision` を AABB sweep に書き直し**:
  - 旧: polygon edge 多重 pass + "inside multi-pass clip"
  - 新: slab method スイープ。`tEnter = max(tEnterX, tEnterZ)` で進入軸を判定し、その軸成分だけ velocity を `tStop` 倍にクランプ。もう一方の軸は素通し → 自然な壁滑り
  - "もし既に AABB 内側" の場合は何もせず素通り (押し戻し起きない)
  - Y AABB 重なりも判定 (上下にズレてれば無視)
- 着地保護: `footY >= prism->groundMaxY - (radius + 0.10)` なら side push スキップ (TopContact 側で処理)

### 3. プレイヤー / 操作系

- **TAB戻り滑らか化**: `Camera_SeedPlayer3DFromCurrent()` で光モード解除時に内部 lerp 状態を再シード (`Switch_Light.cpp` 内で呼出)
- **影モード自動 3D 復帰**: `PlayerModeSwitchManager_ForceTo3D()` 新設。`Player2D_Respawn` で Y<-10 (30f) or `!isGround` (90f) を検知して発火、`Player3D_InitAt` でスポーン地点に再生成
- **ダッシュ固定解除**: `Player3D_Respawn` と `ForceTo3D` で `isDash = false` + `maxMoveSpeed` リセット
- **F モード切替プロンプト抑制**: STAGE_SELECT + 任意のフェード中は `g_ShowBillBoard = false` で早期 return (フェード中の prompt flash を排除)
- **F キー連打の二重バインド対策**: ステージ入場は `IsInputTrigger(ActionKey)` 必須化
- **リスポーン時に全オブジェクト位置リセット**: `Player3D_Respawn` / `Player2D_Reset` / `ForceTo3D` 全てから `Field_ReloadCurrentMap()` + `Collision_ResetShadowContactState()` を呼ぶ

### 4. 箱押し/引き (`Pushing_Obj_Manager.cpp`) 全面再設計

- **トリガを左クリックに分離**: `Mouse_GetState().leftButton` (キーボードF は変身専用に解放)。ゲームパッド B も継続サポート
- **4方向スナップ at grab**: `|dx| ≥ |dz|` で axis を `(±1, 0, 0)` か `(0, 0, ±1)` のいずれかに → 斜めには絶対動かない
- **プレイヤー Yaw も axis に正対固定**: `g_LockedYawDeg = atan2(axis.x, axis.z) + FirstRotation.y`
- **W/S のみ参照、A/D 無視**: キーボード W/S + ゲームパッド左スティック Y のみ
- **カメラ方向で W/S 符号反転**: `dotCam = camFwd・axis` の符号で push/pull が反転 → カメラを 90°越えで反対側に振ると操作感が反転する仕様 (ユーザー指定)
- **速度固定**: `kPushSpeedPerFrame = 0.04` (歩き定常 0.111 の約30%)。カメラ向きで速度変動なし
- **プレイヤーと箱の移動量完全一致**: velocity 経由は damping (×0.925) で乖離するため、**両者とも position を直接書き換え**。velocity は最後にゼロクリア
- **押せない時** (`CanObjectMove` false): プレイヤーも箱も動かさず接触保持

### 5. UI / オプション / 音量

- **ポーズ画面 hit rect 配置**:
  - 旧: EXPLAN/RESET/BACK の height が `1340.0f` (134.0 のタイポ疑い) → 画面下まで突き抜けて全部重複
  - 新: sprite 描画中心座標から逆算した center-based 配置 (`MakeRect(cx, cy, w, h)` ヘルパ)、bar 760×160 / button 260×140
- **Option_Update リライト**:
  - 旧: `if (ms.leftButton)` で押下中ずっと発火 → RESET 連打、BACK で SetFade 毎フレーム
  - 新: click edge 検知 (`leftPressed`) で 1クリック=1アクション
  - **ドラッグ捕捉**: スライダー上で初回クリック時に `s_dragging` に対象を保存、ボタン離すまで `g_mouseX` をクランプ反映 → カーソルが上下にぶれてもバーが安定追従
  - hover も毎フレーム再計算してハイライト/外しを正しく
- **音量バグ**:
  - `SetMasterVolume(g_MasterVolume)` で 0-100 の生値を XAudio2 SetVolume にそのまま渡していた → 50倍の音量爆裂
  - 修正: `SetMasterVolume(g_MasterVolume / 100.0f)` で 0-1 正規化
  - デフォルト 50 → 30 に下げ、Option_Initialize で起動時に1回適用
- **ポーズ中カーソル表示問題 (Esc 関連)**:
  - 旧 `Mouse_SetMode` は `SetEvent(...)` でイベントを立てるだけ、実際の `gMode` 更新は `Mouse_ProcessMessage` の中 (= マウスメッセージが来るまで反映されない)
  - Esc 閉じ → マウス動かさない → mode が ABSOLUTE のまま → カメラ mouse-look が gate される (`positionMode == RELATIVE` が false)
  - **更にイベント auto-reset がキューに溜まる** → 連続トグルで `gMode` が ABS↔REL を毎フレーム振動 → `ms.x/y` が絶対座標 / 相対 delta で振動 → Option UI hover 判定が高速トグル → **UI 全体が点滅**
  - 修正: `Mouse_SetMode` を完全同期実行に改修。`gMode` / `ShowCursor` / `ClipCursor` を即座に呼び、`ResetEvent(gAbsoluteMode/gRelativeMode)` で古いイベントもドレイン。`Mouse_ProcessMessage` の対応 case は dead code 化
- **Title 開始**: Enter キーから左クリック (+ gamepad B) に変更

### 6. オーディオ
- **Crossfade slope 統一**: `FadeInAudio` と `FadeOutAudio/FadeOutAndStopAudio` が同じ `1/(Duration*60)` の傾きに。`FadeInAudio` の `CurrentVolume = 0; SetVolume(0)` リセットを削除 → モード高速切替中の音量ジョルト解消
- **BGM 冒頭 blip 修正**: `game.cpp` Game_Initialize で `SetAudioVolume(0)` を `PlayAudio` の**前**に。新規 SourceVoice のデフォルト音量 1.0 で 1 audio quantum 鳴ってから silence する現象を排除

### 7. プロジェクト構造 / バックバッファ
- **`direct3d.cpp`**: `swap_chain_desc.BufferDesc.Width = 1980; Height = 1080;` をハードコード固定 → debug の小さなウィンドウ (1280×720) でも UI 座標が壁面サイズと一致 (旧: 0=auto で client サイズに従い UI レイアウトが破綻していた)
- **`base.vcxproj` / `.filters`**: `stage_clear_state.cpp/.h` を登録
- **stage1.txt**: 噴水 (`F` セル) を `.` に置換して削除

## セッション 2 (2026-06-05) で完了した修正

### 8. フェード中の全入力ロック (Task 1)
- `fade.h`: `inline bool Input_IsGloballyLocked() { return GetFadeState() != FADE_NONE; }` を追加
- `manager.cpp`: `MenuKey` (Esc/Start) と F2 デバッグキーを `Input_IsGloballyLocked()` で gate
- `game.cpp`: `fadeLocked` を `cineLocked` / `cineActive` に OR、Player3D/2D Update・カメラ・`PlayerPushManager_Update` を全部 gate (`#include "fade.h"` 追加)
- `Switch_Light.cpp`: TAB トグル + 光源移動を fade で抑制 (`#include "fade.h"` 追加)
- `title_manager.cpp`: タイトル開始の左クリック検出を fade で抑制
- 注意: `Option_Update` は意図的に gate しない (UI 内のクリック処理は走らせる)。BACK ボタンは `SetFade` のガードで多重発火を防いでいる

### 9. マウス delta 消費バグ (Task 2 + 追加修正)
**Mouse_ProcessMessage dead code 除去**:
- `WaitForMultipleObjectsEx` で `gAbsoluteMode` / `gRelativeMode` をディスパッチする死コードを削除 (`Mouse_SetMode` 同期化後、誰も SetEvent しないが、stale signal で `ShowCursor` を 1 回だけ呼んで counter unbalance するリスク)
- `WaitForSingleObjectEx(gScrollWheelValue)` 単独に簡略化
- `WM_ACTIVATEAPP` refocus 時に `ShowCursor` ループ化 (`while (ShowCursor(FALSE) >= 0) {}`)、unfocus 時に `ClipCursor(nullptr)` で alt-tab 動作を改善

**Mouse_PeekState API 追加 (本質的な原因)**:
- ユーザ報告「stage_select でカメラがマウスで動かない」の根本原因: **同フレーム内で `Mouse_GetState` が複数回呼ばれると 2 回目以降は relative delta = 0 になる**。`gRelativeRead` event の SetEvent/ResetEvent プロトコルが「1 フレーム 1 consumer」前提
- `PlayerPushManager_Update` が `Mouse_GetState` でボタンだけ読んでいたが、これが先に走ると後続の `Player3DCamera_Update` の delta を奪う → カメラがマウスで回らない
- `title_manager.cpp` の click 検出も同問題 (Title 中は Title_Camera が動かないので顕在化しなかった)
- **修正**: `mouse.h` に `Mouse_PeekState` を新設。`memcpy(gState)` + `positionMode` のみ、`gRelativeRead` への SetEvent はしない。`PlayerPushManager_Update` と `title_manager.cpp` の click 検出を `Mouse_PeekState` に切替
- canonical delta consumer は `Player3DCamera_Update` / `Player2DCamera_Update` / `Option_Update` (絶対座標時) の 3 つ。これらが各フレーム 1 回だけ `Mouse_GetState` を呼ぶ前提

### 10. Option 画面のフレーム点滅 (Task 3)
- `Option.cpp` `Option_Draw`: 全 6 ボタン/バーの hover ハイライト判定から `OptionRect[X].contains(g_mouseX, g_mouseY)` の二重評価を除去、`g_SelectedOption == OPTION_SELECT_X` だけに統一
- 原因: VOLUME / BGM / MOUSE バー rect が y=390/540/690 で高さ 160 配置 → 隣接バーが 10px overlap → カーソルが重複帯にあると `hover` ループは VOLUME (enum 順最先) を返すが、Draw は `OptionRect[BGM].contains` も true で BGM も同時ハイライト → カーソルの 1px ブレで 2 ボタンが交互点滅
- Update に hover 判定が集約されているので Draw 側は g_SelectedOption だけ見れば一意

### 11. ステージクリア演出 5 段階化 + 次ドア霧出現 (Task 4)
**新タイムライン (`stage_clear_state.cpp`)**:
```
Phase 1: zoom-in to cleared door       [0.0, 1.0]  カメラがクリアドアへ
Phase 2: hold + cleared door collapse  [1.0, 2.5]  霧 + ドア崩落 + CLEAR 出現
Phase 3: pan to next door              [2.5, 3.5]  カメラが次ドアへパン
Phase 4: hold + next door reveal       [3.5, 5.0]  霧 + ドアと marker 立ち上がり
Phase 5: zoom-out to player            [5.0, 6.0]  プレイヤーへ復帰
Total: 6.0s
```
- 次ステージがない場合 (STAGE_3 想定の防御): Phase 3/4 をスキップ、total = 3.5s
- 状態追加: `g_CineNextStage`, `g_CineNextDoorPos`, `g_CineNextDoorCamPos/At`, `g_CineClearMistStarted`, `g_CineNextMistStarted`
- ヘルパ: `NextStageOf(stage)`, `FindStageDoorPos(stage, *out)` (GetFieldMap から FIELD_STAGE_N を検索), `ComputeDoorFraming(doorPos, fromAt, *outCamPos, *outCamAt)`
- `StageClearCinematic_Start()`: クリアドア用 framing + 次ドア検索 + 次ドア用 framing を計算
- `GetDoorScaleY()`:
  - クリアステージ: Phase 2 elapsed 0.4-0.7s で 1→0 smoothstep (霧ピーク中に隠れる)
  - 次ステージ: Phase 4 (cinematic 3.9-4.7s) で 0→1 smoothstep (霧の中から立ち上がる)
  - その他: cleared なら 0、それ以外 1
- `field.cpp` のドア描画は既存の `doorScaleY <= 1e-4f` skip ロジックで自動対応 (`!IsUnlocked` skip も通過するため次ステージは描画ループに入る)

**ミストタイミング整合**:
- 旧コードは `StageClearEffect_Start` を `Cinematic_Start` 時 (cinematic t=0) で発火していたが、霧は 0-0.7s なので Phase 2 のドア崩落 (cinematic 1.4-1.7s) には既にミスト無し → 視覚的に裸でドアが崩れる
- 新コードは Phase 2 入場時 (cinematic 1.0s) で発火 → 霧は cinematic 1.0-1.7s で見える → 崩落 1.4-1.7s は霧ピーク中に隠れる
- 同様に次ステージは Phase 4 入場時 (cinematic 3.5s) で発火、霧 3.5-4.2s + ドア立ち上がり 3.9-4.7s で霧の中からの出現演出になる

**`EffectState::everStarted` フラグ追加**:
- 旧 `GetClearAlpha`: `!e.running` → 1.0 を返していた (「effect 完了後の settled 状態」前提)
- 新コードで Effect_Start を Phase 2 まで遅延したため、Phase 1 中は `!everStarted && !running` で初期状態にあたる
- 修正: `running=false` のときは `everStarted` を見て 1.0 か 0.0 を返す。Phase 1 中の CLEAR billboard 早期表示 (regression) を防ぐ

**game.cpp の billboard 描画**:
- 旧: easy/mid/hard マーカーは常に alpha 1.0
- 新: `markerA = isCleared ? 1.0f : doorScaleY` → 次ステージは reveal で alpha と Y-scale が同期、cleared は 1.0 維持 (ユーザ要望)

## セッション 3 (2026-06-05) で完了した修正

### 12. Option UI の当たり判定ズレ + 半透明 BG 透け抜け点滅
**原因**:
- バックバッファ 1980×1080 に対し、デバッグウィンドウは 1280×720。マウスは **window-client** 座標 (1280 系) で来るが、Option UI の sprite 描画と hit rect は **back-buffer** 座標 (1980 系) を使っていた → クリック位置が常に左上ずれ
- `Option.png` と `Bg.png` が **alpha=127 (半透明)** だった (PIL で確認)。BG を描いても Game シーンの billboard (STAGE1 マーカー等) が透けて見える → 「Option を開くと点滅し始める」の正体
- bar 用 hit rect が 760×160 で **隣接 rect と 10px overlap** → カーソルが重なり帯にあると hover 不安定 (前セッションで Draw 側は一意にしたが Update 側 rect 自体も詰める必要あり)
- RESET ボタン押下時に `Option_Initialize()` 全実行 → テクスチャ多重リーク + `Mouse_SetMode(ABSOLUTE)` 再呼出

**修正**:
- `mouse.h/cpp`: `Mouse_GetWindow()` と `Mouse_GetClientSize(out_w, out_h)` を追加
- `Option_Update`: `ms.x/y` を `GetClientRect` 比でバックバッファ空間にスケール → `g_mouseX/Y` がボタン中心と一致
- `Option_Initialize`: hit rect 寸法を **テクスチャ bbox** から逆算して再配置
  - bar: 779×80 (rail 可視幅 = 601..1378 に合わせる、高さ縮め overlap 解消)
  - EXPLAN: 320×90 centered (989, 991)
  - RESET: 370×90 centered (385, 994)
  - BACK: 520×90 centered (1588, 991)
  - bar の minX/maxX = 601..1378 (旧 580..1340 は rail 右端を超えていた)
  - bar handle 初期位置を `g_MasterVolume / g_BgmVolume` から逆算 (起動時 30% → 833 で表示) … 旧は常に screen 中央
- `Option_Draw`: BG sprite の前に **不透明黒 underlay** (`g_BlackTexture` を `BLENDSTATE_NONE` で全画面) → ステージ billboard を完全に隠す → 透け点滅排除
- `Option_Update` の RESET ハンドラ: `Option_Initialize()` を呼ばず、値だけ in-place で再設定 (`g_MasterVolume=30` 等 + bar handle 位置 + 各 setter 呼出)

### 13. ステージクリア演出: 次ドアの 90° 横向き framing 修正 + 霧リアル化
**問題**:
- Phase 4 で次ステージのドアを正面ではなく **右 90° 横** から映してしまっていた
- 原因: `ComputeDoorFraming(nextDoor, fromAt=g_CineDoorCamAt)` が前カメラの look-at 位置 (= クリアドア座標) から方向を派生していた。STAGE_1→STAGE_2 のように両ドアが同一 Z 行に並んでいると方向ベクトルが純 +X になり、カメラがドアの真横に来る
- 霧が薄く、ドア arch を覆いきれていなかった

**修正**:
- `ComputeDoorFramingFront(doorPos, yawDeg, *outCamPos, *outCamAt)` を新設: ドア自身の `rotate.y` から前方向 `(-sin yaw, 0, -cos yaw)` を計算し、その方向に `kCineDoorBackDist` 引いた位置にカメラを置く → 必ず正面ビュー
- `FindStageDoor(stage, *outPos, *outYawDeg)` ヘルパも追加 (位置だけ拾う旧 `FindStageDoorPos` は薄い shim として残置)
- `StageClearCinematic_Start`: 次ドア framing を `ComputeDoorFramingFront` に切替 (クリアドア framing は `ComputeDoorFraming` のままで OK = プレイヤー視点からの自然な引きを維持)
- カメラ距離・高さ拡張: `kCineDoorBackDist 4.0 → 5.2`, `kCineDoorHeight 1.5 → 2.0`, `kCineLookAtYOfs 1.0 → 1.4` → 霧 + ドアアーチがフレームに収まる
- 霧 (`StageClearEffect_DrawSparkles`) 全面リライト:
  - 2 レイヤー構成: コア (40→56 個、近接・濃色) + ドリフト (40 個追加、外周・薄め・上昇) = **96 puffs**
  - サイズ拡大: コア 1.4→1.9、ドリフト 2.6
  - 半径拡大: 2.2→2.6
  - 高さ広がり: 2.0→2.4、ドリフトは時間で **+1.6 上昇** (煙のように立ち上る)
  - alpha エンベロープ smoothstep 化: 0→0.96 (旧 0.85) で立ち上がり、35-75% でピーク維持、75-100% で fade-out
  - 色: コア near-black `(0.04, 0.04, 0.06)`、ドリフト warmer gray `(0.18, 0.17, 0.20, ×0.75)` → 平面じゃない奥行きが出る

## セッション 4 (2026-06-05) で完了した修正

### 14. ステージクリア / ステージ入場時の dash 残留 (Task 1, 優先)
**問題**: STAGE_SELECT で Shift を押しっぱなしのまま F でステージ入場すると、新ステージで Shift を離していてもダッシュ速度を維持。リスポーン経路 (`Player3D_Respawn` / `ForceTo3D`) は既に対策済みだったが、**ステージ入場時の `Player3D_InitAt`** に同じガードが無かった。

**根本原因**:
- ステージ入場 → `Field_LoadMap` の `R` セルで `Player3D_InitAt(g_PlayerStartPos, ...)` を呼ぶ
- `Player3D_InitAt` は state を `PLAYER_STATE_MOVE` にリセットしていたが `isDash` / `maxMoveSpeed` は触れず → 直前の STAGE_SELECT で立っていたダッシュフラグがそのまま継承
- 速度クランプ (Player3D_Move 内 maxMoveSpeed 比較) はダッシュ速度で動く

**修正** (`player3D.cpp:644-`):
- `Player3D_InitAt` 末尾に `g_Player3D.isDash = false;` + `g_Player3D.maxMoveSpeed = g_Player3D.FirstMaxMoveSpeed;` を追加
- これで Respawn / ForceTo3D / InitAt の 3 経路すべてで dash クリアが揃った

### 15. ステージ開始時のカメラスナップ + STAGE_SELECT 固定カメラ
**問題 A**: ステージ入場時、カメラが STAGE_SELECT の構図から TPS 構図へ lerp で滑ってきて挙動が「おかしい」
**問題 B**: STAGE_SELECT のカメラがプレイヤー追従 (TPS) になっていた。ユーザ要望: Title と同じ「固定」が正しい

**修正 A** (`camera.cpp` / `camera.h`):
- `Player3DCamera_Reset()` を新設: `gCamAnglesInit = false` + Yaw 180 / Pitch 15 / Distance 8 / gCamTarget/Pos ゼロにリセット
- `Player3DCamera_Update` に「初回フレーム snap」分岐を追加: `gCamAnglesInit == false` のとき lerp をスキップして desiredTarget/finalPos に即セット、その後 init = true
- 同フレームのマウスデルタも消費抑制 (フェード中の不要な視点ブレ防止)
- `game.cpp` Game_Initialize: `currentStage` が STAGE_1/2/3 のとき `Player3DCamera_Reset()` を呼ぶ

**修正 B** (`camera.cpp` / `camera.h`):
- `StageSelect_Camera_Update()` 新設: 固定ポーズ `Pos=(6.0, 7.5, -4.0)` / `At=(6.0, 1.5, 5.5)` / `Up=(0,1,0)` — `stage_select.txt` 上の player (x=7, z=3) と doors (x=1/6/11, z=8) を全部画面内に収める俯瞰構図
- `game.cpp` Game_Update カメラディスパッチ:
  ```cpp
  if (!cineActive) {
      if (GetCurrentStage() == STAGE_SELECT) StageSelect_Camera_Update();
      else                                   Player3DCamera_Update();
  }
  ```
- ステージクリア cinematic はそのまま動作 (`cineActive` ゲートで全体上書き)

### 16. ステージクリアの煙エフェクト: ドア付近・小規模・細かく
**問題**: 旧コードの霧 (96 puffs, radius 2.6, size 1.9/2.6, height spread 2.4, up-drift 1.6) は広がりすぎてドアから離れ、個々の puff が大きく粗さが目立つ

**修正** (`stage_clear_state.cpp:28-35`):
- パフ数: 96 → **140** (うちコア 56→96、ドリフト 40→44) → 密度倍増で粗さを隠す
- 半径: 2.6 → **1.3** (ドアアーチを抱える程度)
- サイズ: コア 1.9→0.7、ドリフト 2.6→1.0 → 個々のパフが半分以下
- 高さスプレッド: 2.4 → **1.6** (ドア arch 2u に合わせる)
- 上昇ドリフト: 1.6 → **0.7** (穏やかに立ち上る)
- DoorYOffset: 1.3 → **1.0** (ドア中心により密着)
- alpha エンベロープ・色は据え置き (peak 0.96、コア near-black、ドリフト warmer gray)
- `EffectState::sparkleAngle/Height/Speed` 配列は `[kSparkleCount]` で静的サイズ拡張 → 各ステージ 3 配列 × 140 floats だけメモリ増、リビルド不要

**注**: ユーザ報告「Optionの音量バグ・点滅は UI 修正で解決済み」のため追加対応無し

## セッション 5 (2026-06-06) で完了した修正

### 17. 小修正 + 残タスク棚卸し
- `Option.cpp:370` C4244: `SetFade(40.0f, ...)` → `SetFade(40, ...)` (`SetFade` の第一引数は `int fadeframe`)
- `Polygon3D.cpp:382` の `Mouse_GetState`: `Polygon3D_Update` がどこからも呼ばれない dead code であることを確認 (grep 結果 0)。`Mouse_PeekState` への置換不要
- merge残骸ファイル (`player3D_*_55.cpp`, `memo.cpp`) は commit `7b71013` で既に削除済みであることを確認 → CLAUDE.md の残タスク表から除去

## セッション 6 (2026-06-06) — タイトル磨き

### 18. タイトル画面のクリックプロンプト + ロゴ breathe アニメ
- `Title_Arrow.png` (42×56、既存だが未使用) を画面下部 Y=72% に表示。alpha pulse 0.55→1.0 + Y軸 ±8px bounce で「クリック開始」プロンプト化
- ロゴに 1.00→1.025 倍の breathing scale を追加 (待機中のみ)
- `g_PromptFrame` 1 つの timer で両方を駆動
- クリック後の cinematic 中 / フェード中は両方非表示 (`Title_IsActionStarted()` + `GetFadeState() == FADE_NONE` で gate)
- 変更ファイル: `title.cpp` / `title_manager.cpp` (アクセサ追加) / `title_manager.h`
- 注: title.cpp は CP932 のため Python バイト編集で日本語コメント破壊を回避

## セッション 7 (2026-06-06) — ステージセレクト夜化 + 月

### 19. カメラ角度を地面に平行寄りに
- `StageSelect_Camera_Update`: Pos `(6, 7.5, -4)` → `(6, 5.5, -5)`、At `(6, 1.5, 5.5)` → `(6, 2.0, 5.5)`。俯角 32° → 18°
- 後に -7.5, -10, -7.5 と推移 (ユーザー試行錯誤)

### 20. ステージセレクトに月 (光源ボール) 配置
- `stage_select.txt`: Y=1 layer の `3` (床レベル) を Y=3 layer の中央 col 8 に移動 → 月位置 (6, 2, 8)。後にユーザー要望で Y=1 元の位置に復元
- `field.cpp` `Field_DrawLightBalls()` 新設: FIELD_OBJ_3 を**別ループで Light.Enble=FALSE** で描画 → unlit / 全明テクスチャ表示 → 暗い夜空に対する月のような視覚
- `field_Draw` 本体では FIELD_OBJ_3 を skip (重複描画回避)
- `game.cpp` Game_Draw で `field_Draw()` 直後に `Field_DrawLightBalls()` 呼出 + `g_BallLight.SetEnable(TRUE)` で復元

### 21. 夜空 (SkyDome) の暗化
- 一度 `SkyDome.cpp` 内で dim LIGHT wrap を試みたが、title scene の player 描画に副作用 (Player3D_Draw 後の Light state が壊れる) → 元に戻す
- 最終: `game.cpp` の `SkyDome_Draw()` 呼出を `LIGHT night = { Enble=TRUE, Ambient=(0.22, 0.22, 0.34), Diffuse=(0.10, 0.10, 0.18), Direction=(0,-1,0) }` で wrap → 暗い青みがかった夜空。後に `g_BallLight.SetEnable(FALSE)` で復元

## セッション 8 (2026-06-06) — 壁 4 倍化 + スポットライト

### 22. ステージセレクトの壁を 4 倍層に
- ユーザー要望: 各 box 4x ではなく**現在の壁の層数を 4 倍**に
- `field.cpp` の `LoadMapFromFile`: `isStageSelect` フラグ追加、ロード後に各 `FIELD_WALL` を `pos.y + 3, +6, +9` でクローン → 3 層 × 4 = 12 層スタック
- 各 block は scale 1 のまま (大きくはしない)
- コリジョンは元の 3 層のみ (視覚のみ)

### 23. スポットライト化 (shader レベル)
- `shader_pixel_2d.hlsl`: 新 cbuffer `Buffer6` (`IrisCenter`, `IrisRadius`, ... 後で iris 追加分も同 slot) と並び `Buffer6` だが内容は spotlight。実際は **Light cbuffer (b2) を repurpose** → `Light.Dir.w >= 0.5` で spotlight mode 有効化。`Light.Dir.xyz` を cone direction、cone 角 `cosOuter=0.55 / cosInner=0.85` でハードコード、`smoothstep` で attenuation
- `game.cpp` Game_Initialize: STAGE_SELECT で `BallLight.SetDirection(0,0,1,1)` → +Z 方向 (奥の壁) に照射
- 他ステージ (STAGE_1/2/3) のデフォルトは `(0,0,0,0)` → 通常 point light
- `title_manager.cpp` の `SetDirection(LightPos.x, .y, .z, 1.0)` の `.w=1.0` → `.w=0` に変更 (誤って spotlight 化されないように)

### 24. カメラ水平化 + 引き
- `Pos.z = -7.5`、`Pos.y = 3.5`、`At.y = 3.0`、`At.z = 6.5` で約 2° 俯角
- 12 層壁の天井方向を見せるための高さ調整

## セッション 9 (2026-06-06) — 追従カメラ + 再入場 + 演出簡略化

### 25. ステージマーカー拡大 + クリア billboard 位置調整
- `game.cpp` の billboard ループ: stage marker `size 1.0 → 2.0`、`pos.y + 2.0 → + 2.5`
- CLEAR billboard `size 1.0 → 2.0`、`pos.y + 3.2 → + 4.8` (大きいマーカーの上に乗せる)
- 後にユーザー要望で CLEAR は `2.0 → 2.0/3.0` (3:1 アスペクト保持)、`pos.y + 4.0` に微調整

### 26. ステージセレクトに「遅延追従 + クランプ」カメラ
- `StageSelect_Camera_Update`: 静的 `s_camX`、player.x を `lerp 0.08` で追いかけ、両端壁の向こう側が映らないよう `x ∈ [4.5, 8.5]` でクランプ (FOV 45 + camera.z=-7.5 + 壁 x=-2/15 から算出)
- 初回 snap (`s_camXInit`) で前シーンの古い x からの lerp 回避
- 後の `StageSelect_Camera_Reset()` (camera.h 公開) で stage 切替時にリセット可能化

### 27. クリア済みステージ再入場 (Re-enterability)
- `player3D.cpp` `Player3D_CheckGoal`: STAGE_1/2/3 ドア入場の `if (IsCleared) return;` を削除 → クリア後でも F で再入場可能。Lock check は残存
- `MarkCleared` は冪等 (既に cleared なら何もしない) なので再クリアでも cinematic は発火しない

### 28. ドア消滅カット + 黒い靄を Phase 2 から削除 (後にこのセッションで復活)
- `stage_clear_state.cpp` `GetDoorScaleY`: クリア済 → 0.0 を 1.0 に変更 (ドア常時表示) → 再入場可能の前提
- Phase 2 の `StageClearEffect_Start(g_CineStage, ...)` 呼出削除、代わりに `g_Effect[g_CineStage].everStarted = true` だけ立てて CLEAR billboard を即表示

## セッション 10 (2026-06-06) — フェード中カメラ + 点滅修正第一弾

### 29. シーン切替直後のカメラが前シーンのポーズで残る根本原因
- 原因: `game.cpp:388` の `cineActive = StageClearCinematic_IsActive() || fadeLocked` がフェード中もカメラ更新を skip
- 結果: 新シーンの fade-in 開始時、前シーンのカメラポーズが映ったまま 1 秒間 fade-in → fade 完了後にスナップ
- 修正: `cineActive` から `fadeLocked` を除外 (`StageClearCinematic_IsActive()` のみ)。`cineLocked` (入力ロック) は据え置き

### 30. billboard 点滅 (PushManager / ModeSwitchManager) 防衛策
- `Pushing_Obj_Manager.cpp` `PlayerPushManager_Draw` / `PlayerModeSwitchManager.cpp` `_Draw` 冒頭に `SetBlendState(BLENDSTATE_ALFA) + SetDepthTest(TRUE)` を明示設定
- 直前の Player3D_Draw が残した state を継承していたため、Option が state を perturbe した後に点滅
- `Option.cpp` `Option_Draw` 冒頭に `SetDepthTest(FALSE)`、末尾に `SetBlendState(NONE) + SetDepthTest(TRUE)` で復元

## セッション 11 (2026-06-07) — Iris transition 1 次実装 + 静止カメラ化

### 31. F でステージ入場時の「カメラ突然回転」を iris で隠蔽
- 原因: F 押下と同時に `SetCurrentStage(STAGE_X)` + `SetFade(60, FADE_OUT, SCENE_GAME)` で current stage が変わる → 次フレームから `Player3DCamera_Update` (TPS rig) が dispatch される → StageSelect の固定ポーズから TPS への lerp で「変な方向に回転」
- 対症療法 + 抜本対策: iris-out 演出 (円が縮んで黒くなる) で乗り換え瞬間を隠蔽

### 32. iris モジュール新規 (`iris.cpp/.h`)
- 状態機械: `Idle` → `Shrinking` → `BlackHold` → `Idle`
- `Iris_Start(centerScreenPx, durationFrames, scene, stage)`: ドアの screen 座標を中心に back-buffer 対角線の半径から `ease cubic` で 0 へ縮小
- `Iris_Update`: 毎フレーム timer + radius 更新、完了時 `SetCurrentStage + SetScene + SetFade(60, FADE_IN)`
- `Iris_ApplyToShader`: `Shader_SetIris(center, radius, active)` で b6 cbuffer 更新 (毎フレーム Manager_Draw 冒頭で呼出)
- `Iris_IsActive`: 入力 + カメラ gate 用

### 33. shader b6 cbuffer 新規 + cutout ロジック
- `shader_pixel_2d.hlsl`: `cbuffer Buffer6 : register(b6) { float2 IrisCenter; float IrisRadius; float IrisActive; };`
- main() 内、shadow pass mode check の直後 / normal pass の直前: `if (IrisActive > 0.5f && dist > IrisRadius) return float4(0,0,0,1);`
- `shader.cpp/.h`: `Shader_SetIris()` + b6 cbuffer 作成 + PS slot 6 にバインド
- `base.vcxproj/.filters`: `iris.cpp/.h` 登録

### 34. player3D.cpp F 入場で iris 起動
- `Player3D_CheckGoal` FIELD_STAGE_X 分岐: `SetFade` の代わりにドア world pos を `view * proj` で screen に投影し `Iris_Start(screen, 60, SCENE_GAME, STAGE_X)`
- `game.cpp` `cineActive`/`cineLocked` に `Iris_IsActive()` を OR で追加 → iris 中はカメラ凍結 + 入力ロック

### 35. プレイヤー浮き (シーン開始時) 修正
- 原因: R マーカー解析 `pos.y = (y-2)` で Y=1 layer の R は pos.y=0 → 地面 (Layer Y=0, pos.y=-1, BOX_RADIUS=0.5) の天面 -0.5 より 0.5 単位上 → 重力で落下するまで「浮いている」状態が見える
- 修正 (`field.cpp`): R 解析時 `pos.y = (y-2) - 0.5f`

### 36. カメラ自由回転 + ホイールズーム削除
- ユーザー要望: STAGE 全部で固定カメラ、向き変更不要、zoom 不要
- `camera.cpp` `Player3DCamera_Update`: マウスデルタ累積 (yaw/pitch)、右スティック look、`gDistance -= scrollWheelValue * zoomSpeed` を全削除
- yaw=180 / pitch=15 / distance=8 を `Player3DCamera_Reset` で初期化、以後固定
- STAGE_3 用に Player3DCamera_Update を残置 (将来別カメラ想定)

### 37. STAGE_1/2 を stage_select 方式の固定カメラに dispatch
- `game.cpp` dispatch: `STAGE_SELECT / STAGE_1 / STAGE_2` → `StageSelect_Camera_Update`、`STAGE_3` のみ `Player3DCamera_Update`
- `StageSelect_Camera_Reset()` 公開 (camera.h) → `Game_Initialize` で stage 切替時に呼出

## セッション 12 (2026-06-07) — Iris 2 次調整

### 38. ダッシュモーションがジャンプ後永続化
- 原因: Shift トリガーで state = DASH → ジャンプで state = JUMP → 着地後 state = MOVE。`Player3D_Dash()` の Shift 解放チェックは state==DASH 時のみ実行されるため、isDash フラグが永続
- 修正 (`player3D.cpp` `Player3D_Move` 冒頭): Shift 解放時に `isDash=false` + `maxMoveSpeed` リセット

### 39. iris 中心位置 + 縮小速度
- ドア y オフセット `+1.5 → +0.5` (アーチ中央でなくドア中央)
- 縮小時間 `60 → 120` フレーム (1秒 → 2秒)
- ease を cubic → linear に変更 (snap shut の唐突感を解消)

### 40. iris BlackHold + 直接 SetScene + FADE_IN
- 旧 `SetFade(60, FADE_OUT)` は無駄な 60 フレーム黒幕を挟んでいた
- 新: shrinking 完了で `SetScene` 直接呼出 + `SetFade(60, FADE_IN)` でフェードイン
- BlackHold は FADE_IN になるまで iris を active のままにして scene swap 一瞬の黒幕を保証

## セッション 13 (2026-06-07) — 2D スプライト点滅 ROOT CAUSE 発見

### 41. 2D スプライト点滅の真の原因 (これまで対症療法を繰り返してきた)
- **原因**: `sprite.cpp` `DrawSprite` 系 + `Bill_Board.cpp` `DrawBillBoard` が **頂点バッファに boneIndex / boneWeight / normal を書き込んでいない**
- `Map(WRITE_DISCARD)` で取得した動的バッファのスロットには前回のゴミデータが残存
- `boneWeight` が非ゼロのとき頂点シェーダー (`shader_vertex_2d.hlsl`) の skinning 分岐がゴミのボーン行列で頂点を変形 → 頂点位置がフレーム毎に乱数化 → 点滅
- 修正: `DrawSprite` (5 関数) + `DrawBillBoard` の冒頭で `ZeroMemory(v, sizeof(Vertex3D) * N)` → boneWeight が必ず 0 になり skinning 分岐が発火しなくなる
- これによりタイトルロゴ / Option / billboard / その他全 2D スプライトの点滅が一気に解消

### 42. iris 円の center 1 pixel 残り
- shader の `dist > IrisRadius` → `dist >= IrisRadius` で radius=0 時に中心 1 pixel が透過する問題を解消

### 43. CLEAR billboard アスペクト
- `clear.png` 1500x500 = 3:1 の縦横比に対し size 2.0x2.0 で表示していて縦長に見えた
- 修正: `size = (2.0, 2.0/3.0)`、`pos.y + 4.0` に変更

### 44. Option デフォルトボタンのバグ
- `g_MouseSensYaw = GetMouseSensYaw()` (現在値) はリセットになっていない → 固定値 1.0 へ
- `s_dragging` クリア + `g_ShowExplan = false` 追加で「Default 後の当たり判定崩れ」の根本原因 (drag 状態残留) を解消

### 45. Esc + Explan の動作
- `Option.h/.cpp` に `Option_IsExplanOpen()` / `Option_CloseExplan()` 公開
- `manager.cpp` Esc 処理: Explan が開いていれば Option ではなく Explan のみ閉じる → 「次回 Esc で操作説明が表示される」バグを修正

### 46. STAGE3 光源操作の重さ
- 原因: STAGE3 は 28x14x9 ≈ 数千 cell でうち多くが地面。`game.cpp` のシャドウキューブマップパスは 6 面 × 全 map objects 描画
- 修正 (`game.cpp`): キューブマップパスで `FIELD_GROUND` と `FIELD_EMPTY_BOX` を skip → STAGE3 で数百ドローコール削減

## セッション 14 (2026-06-07) — クリア演出復活 + 主人公出現 + ライト調整

### 47. クリアドア + 次ドア両方を「正面」フレーミング
- `stage_clear_state.cpp` `StageClearCinematic_Start`: クリアドア framing も `ComputeDoorFraming` → `ComputeDoorFramingFront(yaw)` に切替 (yaw は `FindStageDoor` で取得)
- 次ドアと同様、ドアの arch 正面から映す

### 48. ステージドア 2 倍スケール
- `field.cpp` ロード時: `FIELD_STAGE_1/2/3` の scale を `(2,2,2)` に
- コリジョンは BOX_RADIUS のままなので入場位置は変わらず、視覚のみ拡大

### 49. iris 完全閉鎖 + linear ease + FullClose hold
- 新規 `FullClose` フェーズ (radius=0 で 15 フレーム保持) を Shrinking と BlackHold の間に挿入
- `SetScene` の blocking work (Game_Initialize の model/texture ロード) 中に前フレームが残って「小さな丸が残ったまま」見える問題を解消
- ease cubic → linear で snap を解消

### 50. BGM 1.5 秒クロスフェード
- `iris.h/.cpp` `Iris_StartBgmFadeOut(bgmId)` 公開
- `game.h/cpp` `Game_GetBgm3DId() / Game_GetBgm2DId()` 公開
- `player3D.cpp` ドア入場時に `Iris_StartBgmFadeOut(Game_GetBgm3DId())` 呼出 → `FadeOutAndStopAudio(1.5s)` で stage_select BGM を滑らかに消す
- `game.cpp` `Game_Initialize`: `g_Bgm3D` を vol=0 で開始 + `FadeInAudio(1.5s, 1.0)` で新ステージ BGM をフェードイン

### 51. ステージライトを真下スポットライト + 全体暗化
- `game.cpp` `Game_Initialize` で STAGE_1/2/3 の場合 `BallLight.SetDirection(0,-1,0,1)` → 真下スポットライト
- `BallLight.SetAmbient (0.08, 0.08, 0.10)` → `(0.04, 0.04, 0.06)`、`Diffuse (1.00, 0.95, 0.85)` → `(0.85, 0.80, 0.70)` で全体を薄暗く
- 光球自体は `Field_DrawLightBalls` で unlit 描画なので存在感維持

### 52. Phase 2 でクリア済ドアから主人公出現
- ユーザー要望復活: 黒い靄を発生させながら主人公がドアから歩いて出てくる演出
- `stage_clear_state.cpp` Phase 2 入場時:
  - `StageClearEffect_Start(g_CineStage, mistPos)` で黒い靄スポーン
  - プレイヤーを `g_CineDoorPos` にテレポート、`isAuto=true`、`blockMovement=true`、`CurrentAnimIndex = WALK`
  - ドアの yaw から `front direction = (-sin yaw, 0, -cos yaw)` を算出し、毎フレーム `Position += dir * 1.8 / 60` で前進
- 1.5 秒経過後: `CurrentAnimIndex = IDLE`、velocity=0
- Phase 5 終了時: `isAuto/blockMovement` 解除、IDLE に戻して player3D に制御返却
- ドア 2 倍化に伴いカメラ framing 調整: `kCineDoorBackDist 5.2→7.5`、`Height 2.0→2.4`、`LookAtYOfs 1.4→1.8`

## セッション 15 (2026-06-07) — game.cpp 責務分離リファクタ

### 53. 3 モジュール抽出
**新規ファイル** (それぞれ静的状態 + API)
- `game_bgm.cpp/.h` — 3D/2D BGM ライフサイクル + モード切替クロスフェード (旧 g_Bgm3D/g_Bgm2D/g_PrevMode + Update 内の crossfade ロジック移動)
- `game_shadow.cpp/.h` — `g_BallLight` / `LightPos` / `g_ShadowConfig` / シャドウキューブマップ 6 面パスを所有。`GameShadow_BallLight()` で外部から enable トグル可能
- `game_camera.cpp/.h` — ステージ別カメラ振り分け (STAGE_1 → StageOne / STAGE_SELECT, STAGE_2 → StageSelect / STAGE_3 → Player3D)。2D デバッグ KK_LEFTALT トグルもここに移動

**game.cpp 残置**
- `Game_Initialize/Finalize/Update/Draw` のオーケストレーション
- マップロード + dissolve queue + cinematic queueing
- billboard 描画 (stage マーカー / CLEAR / 霧)
- 夜空 LIGHT wrap + SkyDome / SwitchLight 描画

**サイズ**: game.cpp 681 → 394 行。`Game_GetBgm3DId/2DId` は薄い forward に変更 (iris.cpp / player3D.cpp の呼出はそのまま動作)。
**ビルド検証**: x64 Debug 成功、エラー 0。`base.exe` 生成。

## セッション 16 (2026-06-07) — Title / Option / Pause UI 全面再設計

### 54. アセット 16 ファイル投入
| 種別 | 場所 |
|---|---|
| Title ロゴ | `asset/texture/UI/title_logo.png` (1350×422 cropped) |
| Option ページ BG (4 種) | `Option/{explain,volume}_{title,pause}.png` |
| Pause オーバーレイ | `Option/pause_bg.png` |
| 確認ダイアログ | `Option/confirm_delete.png` (800×400) |
| スライダ handle | `Option/slider_handle.png` (44×44 ダイヤ) |
| キーアイコン (7 種) | `Keyboard/key_{F,W,A,S,D,Space,LShift}.png` |

Layout 座標は PIL で BG から抽出して back-buffer 1980×1080 空間にプレスケール。

### 55. Option.cpp/h 全面書き換え
- `OPTION_MODE_{TITLE,PAUSE}` モード分岐 (PAUSE は データ消去ボタン無効)
- 4 ページ: EXPLAIN (操作方法) / VOLUME (BGM+SE スライダ + Reset) / DELETE (action only → confirm dialog) / BACK (action)
- 確認モーダル (NO/YES) を持つ階層 Esc handler 化
- Esc 階層: 確認モーダル → モーダル close、Option → close、無し → mode 適合で Open
- 旧 OPTION_SELECT enum / Master+Mouse 感度スライダーは削除
- マウス座標 client → back-buffer 自動スケール (`Mouse_GetClientSize`)

### 56. Audio.cpp: BGM/SE 音量グローバル
- `SetBgmVolume/GetBgmVolume`、`SetSeVolume/GetSeVolume` 追加 (0..1 正規化)
- `InitAudio` で master を `g_BgmVolume = 0.5` でシード (起動時の爆音回避)
- 現状 SE 系統は未実装、g_SeVolume は将来用に保存のみ

### 57. title.cpp ロゴ差し替え
- `asset/texture/logo.png` → `asset/texture/UI/title_logo.png`
- 中央 → 左上配置 (Pos = (40+W/2, 30+H/2), Size = 720×225)
- BlendState NONE → ALFA に変更

### 58. manager.cpp Esc 階層化
- 旧 `g_OptionMenu` トグル → Option_IsOpen() / Option_IsConfirmOpen() ベースの階層判定
- scene 別 mode 自動選択 (`SCENE_TITLE` → TITLE モード、`SCENE_GAME` → PAUSE モード)

### 59. stage_clear_state: 進捗削除 API
- `StageClearState_DeleteAllProgress()` 追加 (現状は `StageClearState_Reset()` の薄い alias)
- 将来セーブ実装時にここを書き換える前提

## セッション 17 (2026-06-07) — Option フォーカス枠 + ゲームパッド ナビ

### 60. 黄色フォーカス枠
- 1×1 白テクスチャを Option_Initialize で生成 (`D3D11_USAGE_IMMUTABLE`)
- `DrawColorRect` + `DrawFocusFrame` (4 thin rect = 上下左右辺) で矩形枠を描画
- 色 (1.0, 0.85, 0.1)、厚み 4px、不透明 100%

### 61. ナビゲーション状態
- `FocusBtn` enum (BTN_EXPLAIN/VOLUME/DELETE/BACK/BGM/SE/RESET/NO/YES + NONE)
- `BuildNavList`: 現在の状態 (mode / page / confirm) から有効ボタン順序を生成
- マウス移動: `HitTestMouse` で hit ボタンをフォーカス
- ゲームパッド DPad/LStick 上下: list 内をクリック移動
- 左右: BGM/SE フォーカス時のみスライダ値 ±0.01 (連続押し)
- JumpKey (A ボタン) で `ActivateButton`

### 62. ハイライト規則
- マウスホバー & コントローラー選択を統一してフレーム描画
- PAUSE モードでは BTN_DELETE がナビリスト除外 → ホバー枠も出ない
- スライダドラッグ中はフォーカス強制 BGM/SE

## セッション 18 (2026-06-07/08) — クリア演出 / STAGE_1 整理 / 2D 衝突修正

### 63. クリア演出タイミング修正
- 次ドア reveal を `kRevealStart 0.4 → 0.75` / `kRevealEnd 1.2 → 1.35` に変更
- 霧は Phase4 で 0〜0.7s 表示なので、reveal は霧完全消失 (0.7s) の後に開始
- 4.25〜4.85s で 0→1 smoothstep

### 64. 主人公 emerge 修正
- ムーンウォーク: `Rotation.y = yawDeg + 180` → `Rotation.y = -yawDeg` (forward = `(sin Ry, 0, -cos Ry)` と `(-sin yawDeg, 0, -cos yawDeg)` を一致)
- 浮き: `Position.y = doorPos.y` → `doorPos.y - 0.5f` (R-spawn と同じセル中心→足元オフセット)

### 65. STAGE_1 光源 / カメラ
- spotlight: `(0,-1,0)` → `(-1,0,0)` (-X 壁照射、ユーザ「光源で照らされている壁の方向にカメラを向けて」)
- 新規 `StageOne_Camera_Update`: +X 側 Pos(22, 3.5, Z) → -X At(6, 3.0, Z)、Z は player.z 追従 (lerp 0.08、clamp 4..20)
- game_camera dispatch: STAGE_1 → StageOne、STAGE_SELECT/STAGE_2 → StageSelect、STAGE_3 → Player3D

### 66. STAGE_1 マップ
- `stage1.txt` 全 E → W 変換で側面壁可視化
- Y=7〜13 の 7 レイヤー追加 (+7 ブロック高く、ユーザ要望)
- 右側列 (col 17) 全 W → `.` (光源側完全開放、コの字維持)
- 上下ボーダー (各レイヤーの先頭/末尾行) は WWW... のまま

### 67. ドア通過時の靄
- `player3D.cpp Player3D_CheckGoal` で:
  - FIELD_GOAL ヒット時 → `StageClearEffect_Start(currentStage, goalPos+2Y)` (クリア瞬間)
  - FIELD_STAGE_X (ステージ入場) iris 開始時 → `StageClearEffect_Start(targetStage, doorPos+1.5Y)`
- `game.cpp Game_Draw`: 既存のステージマーカーループ後に「現在 stage の靄」追加描画 (FIELD_STAGE_X 不在のプレイステージでも描画)

### 68. 影モード 3D 壁押し戻し修正
- 新規 `Player2DField_VelocityClamp()`: 位置更新前に予測カプセル位置を Resolve_Capsule2D_OBB で検査
- 法線方向 (壁面 → カプセル) の速度成分が負 (= 壁に向かう) なら成分削除、接線は維持
- 4 回反復 (chained box)
- 既存の reactive `Player2DField_Collision` は安全網として残置
- E (FIELD_EMPTY_BOX) も Field_IsSolid に含まれているので透明壁にも適用

## セッション 19 (2026-06-08) — Esc / pause 分離 + ダイヤ枠 + STAGE_1 影フィルタ

### 69. Pause / Option overlay 分離
- `PauseStage` enum 追加 (PAUSE_STAGE_BG / PAUSE_STAGE_OPTION)
- Esc in game → PAUSE_STAGE_BG (pause_bg.png のみ、option page 描画なし)
- pause_bg 上クリック → PAUSE_STAGE_OPTION (option page 描画、pause_bg なし)
- Esc → manager.cpp 階層 Esc handler で全 close
- TITLE モードは常に PAUSE_STAGE_OPTION (pause_bg 使わず)
- 旧仕様: pause_bg + option page 同時描画 → ユーザ「同時に開いてしまっています」

### 70. スライダ hit 領域圧縮
- 旧: width = rail + 80px padding、height 90 → ユーザ「広く取りすぎ」
- 新: width = rail 実幅 (kRailMaxX - kRailMinX = 536)、height 50

### 71. スライダフォーカスはハンドルにダイヤ枠
- `DrawDiamondFrame(cx, cy, half, thickness)`: 4 つの 45°回転矩形でダイヤアウトライン
- BTN_BGM / BTN_SE フォーカス時のみダイヤ枠 (ハンドル位置 = BarXFromT(value) で算出)、他ボタンは従来矩形枠
- diamond half = 30, thickness = 4 (ハンドル 44px の外側に ~8px の枠)

### 72. STAGE_1 影モードフィルタ
- `game_shadow.cpp GameShadow_Update`: shadow.isValid 後に
  ```cpp
  if (st == STAGE_1 && shadow.n.x < 0.7f) continue;
  ```
- → STAGE_1 では receiver normal +X 方向 (= -X 壁) の prism のみ collision に push
- 結果: 影モード walkable は背面壁のみ、左右壁は影モード進入不可
- ユーザ「影状態になれるのは画面奥側の壁のみで、左右にある壁には基本入れないように」

### 73. STAGE_1 マップ右側完全開放
- `stage1.txt` 全レイヤー Y=1〜13 の中間行で col 17 の W → `.`
- 上下ボーダーは WWW... のまま (3 辺壁 = コの字キープ)

## セッション 20 (2026-06-08) — TAB camera 連続性 + クリア演出フロー再設計 + STAGE FPS 60

### 74. TAB 光源カメラのリジッドオフセット追従
**問題**: TAB 押下で光源モードに入る瞬間、カメラがプレイヤーカメラとは異なるポーズへ向けて lerp して「乖離」が見える。旧 `LightCamera_Update` はステージ別固定 yaw (270/90/etc.) で計算した targetPos へ 0.12 lerp。プレイヤーカメラから乖離した位置に滑っていくため不自然。

**修正** (`camera.cpp`):
- 新規 static `g_CamLight_OffsetCaptured` / `g_CamLight_PosOffset` / `g_CamLight_AtOffset`
- TAB 入った最初のフレームで `(g_CameraObject.Position - lightPos)` と `(.AtPosition - lightPos)` を捕捉
- 以降は毎フレーム `position = lightPos + posOffset` / `atPosition = lightPos + atOffset` で剛体追従
- → 光源を A/D で動かすとカメラがそのまま付いてくる。構図は TAB 直前のプレイヤーカメラと完全一致
- `Switch_Light.cpp` TAB exit: `Camera_ResetLightState()` を呼び OffsetCaptured フラグをクリア → 次の TAB 入場で再キャプチャ

### 75. TAB 抜けの stage_select / stage_one カメラもスムーズ移行
**問題**: TAB 抜けると `StageOne_Camera_Update` / `StageSelect_Camera_Update` が直接 `g_CameraObject.Position = desiredPos` で代入。光源ポーズから固定ポーズへ瞬間スナップ。

**修正** (`camera.cpp` / `camera.h`):
- `gStageSelectCamTransFrames` / `gStageOneCamTransFrames` 追加
- `StageSelect_Camera_BeginTransition(int)` / `StageOne_Camera_BeginTransition(int)` 公開
- カウンタ > 0 のとき `g_CameraObject = Lerp3(g_CameraObject, desired, 0.15)` で smooth recovery、その後デクリメント
- `Switch_Light.cpp` TAB exit で両方を 30 frames で発動

### 76. クリア演出フロー再設計 (Phase 1 廃止 + 即時起動)
**ユーザ希望**:
- クリア瞬間ですでにクリアドアにフォーカス
- 主人公がクリアドアから手前へ歩いてきて立ち止まる
- CLEAR 表示
- 次ステージドアへカメラ pan
- 次ドア出現演出

**旧フロー問題**:
- `StageClearCinematic_Update` は `GetFadeState() == FADE_NONE` まで pending → fade-in 中は通常 stage_select 構図 (全ドア丸見え)
- Fade 完了後に Phase 1 (zoom-in lerp) でクリアドアへ → 次ドアが briefly 見える

**修正** (`stage_clear_state.cpp`):
- **Phase 1 完全廃止**: `kCineZoomInSec = 0.0f`。Phase 1 ブランチ削除 (C4723 回避にも寄与)
- **即時起動**: `if (g_CinePending && !g_CineActive)` で FADE_NONE 待ちなく start
- **Fade-in 中はカメラ凍結**: `if (GetFadeState() != FADE_NONE)` で `SetCameraPosition/AtPosition` のみ実行して return (timer 進めない) → fade-in 中ずっとクリアドア構図
- **Start で player を即テレポート**: door 位置に置いて IDLE (fade-in 中に間違った位置が見えない)
- **Phase 5 end pose 計算**: stage_select default ポーズ `(emergeX, 3.5, -7.5) → (emergeX, 3.0, 6.5)` を `g_CineStartPos/At` に設定 (旧: g_CameraObject snapshot だった)
- **Walk 短縮 + idle 拡張**: `kCineEmergeWalkSec = 1.2f` / `kCineHoldClearSec = 2.0f` → 1.2s 歩いて 0.8s 立ち止まり、その後 Phase 3 pan
- **`kCineEmergeWalkSec` / `kCineEmergeSpeed` を file-scope 定数化**: Start と Phase 2 で値同期
- **Cinematic end で `StageSelect_Camera_Reset()` 追加**: 次フレーム `gStageSelectCamX = playerX` スナップ → ジャンプ無し

## セッション 21〜29 (2026-06-08 〜 2026-06-09) — UI / ジャンプ / 影モード / 演出 大改修

### 79. スライダ Handle の travel inset
- `kHandleTravelMinX/MaxX = kRailMin/Max ± halfHandle` で BarXFromT/TFromBarX をハンドル中心が左右端と一致するよう変更
- t=0 / t=1 でハンドルがレール外にはみ出る UX 問題解消

### 80. 起動解像度 1920×1080 化 + 全座標系統一
- `direct3d.cpp` バックバッファ 1980 → **1920**、`main.cpp` SCREEN_WIDTH も 1920
- 旧 1980 prescale (×1.03125) のサイドバー/レール/ボタン座標を全部 1920 系へ revert
- Title / Pause hit rect も 1920 ソース座標そのまま (テキスト bbox 中央)

### 81. Title 画面の全面再構築 (Title_Menu.png + 3 ボタン)
- `asset/texture/UI/Title_Menu.png` を全画面アルファ重ね描画
- 3 ボタン (Option / Start / Exit) の hit rect を画像 bbox 実測値で配置
- マウスホバー / 左クリックで即実行 + 黄色 4px フォーカス枠
- 矢印キー / DPad / LStick で選択切替、Space / Enter / pad A で確定
- 旧「画面クリックで開始」を `Title_RequestStartAction()` 公開関数化、Start ボタンからのみトリガ
- ロゴ (title_logo.png) と矢印プロンプト (Title_Arrow.png) は撤去 + 該当ロード削除
- マウスカーソル可視: `Title_Initialize` で `Mouse_SetMode(ABSOLUTE)`、`manager.cpp` の Option 開閉時の mode toggle を scene-aware に (TITLE は close 後も ABSOLUTE 維持)
- Exit ボタン → `PostMessage(WM_CLOSE)`

### 82. Option ページ多数バグ修正 + 7 行化
- データ消去テキスト色の正しい振り分け: TITLE=白 (active) / PAUSE=灰 (disabled)
  - explain_title/pause, volume_title/pause 両ページとも `(s_Mode == OPTION_MODE_TITLE) ? *_title : *_pause` で統一
- ボリュームページ画像から手本スライダハンドルを除去: pause 版 (ハンドル無 + 白消去) を title.png に流用、その複製のデータ消去領域を 0.4 倍暗化して pause.png に上書き → 両方ハンドル無
- スライダフォーカス枠: ダイヤ枠廃止 → 矩形枠 (高さ 80) でバー全体を囲むスタイルに戻す
- Enter / Space / pad A で選択中ボタン確定 (JumpKey=B 廃止に対応)
- EXPLAIN ページを 6 → **7 行化** (光源操作 行追加)
- `kRowY[7] = {225, 338, 465, 589, 715, 838, 965}`
- Key 列: Row 5 (選択) を mouse_left.png (96×96 大きめ)、Row 4 (光源操作) を key_TAB.png (170×80、901×500 にクロップ済)
- Pad 列: Row 0=LStick / 1=B / 2=A / 3=A / 4=X / 5=Y / 6=DPad、`kPadColCx = 1597`

### 83. Pause 画面を 4→5 ボタンに拡張 + 黄色枠ナビ
- `pause_bg.png` を「Back to Game / ReStart / Option / Stage Select / Stage Exit」5 ボタン版に差し替え
- `BTN_PAUSE_RESUME / RESTART / OPTION / STAGE_SELECT / EXIT` enum 追加、hit rect 個別に配置 (1920 系)
- 旧「どこをクリックしても option」削除、BuildNavList 経由で 5 ボタンを通常のフォーカス／ナビ機構に乗せた
- アクション: Resume = Option_Close、ReStart = `Player3D_Respawn`、Option = PAUSE_STAGE_OPTION、Stage Select = STAGE_SELECT へフェード、Stage Exit = SCENE_TITLE へフェード
- 黄色 4px フォーカス枠を選択中ボタンに描画

### 84. キーバインド Switch レイアウト化
- `newKeyBind.cpp`:
  - JumpKey: A → **B**
  - ActionKey / ChangeKey: B → **A**
  - DashKey: X → **Y**
  - LightKey: Y → **X** (Dash と衝突回避)
  - EnterKey: B → **A** (Result 確定)
- `Pushing_Obj_Manager.cpp`: pad の Push を B → **RIGHT_SHOULDER** (Jump と衝突回避)
- Option EXPLAIN 表示も同じ Switch アイコンに整合

### 85. ボタンクリック効果音 (SystemSE_PlayClick)
- `Audio.h/cpp` に `SystemSE_PlayClick()` 追加、`InitAudio` で `asset/Audio/button_click.wav` をプリロード
- 毎回 `GetSeVolume()` を反映して再生 → Option SE スライダで音量調整可
- Title 3 ボタン (`TitleActivate`) と Pause/Option 全ボタン (`ActivateButton`) の冒頭で呼出
- 元素材は MP3 だったので ffmpeg (winget 経由) で WAV 変換

### 86. Game Clear 画面 (STAGE_3 後)
- `asset/texture/UI/game_clear.png` を Result シーンで全画面描画
- `Result.cpp` リライト: Enter / Space / 左クリック / pad A 等いずれの入力でも SCENE_TITLE へフェード

### 87. 進捗セーブ実装
- `save.dat` (exe 直下、1 byte = 最高クリアステージ 0-3) 実装
- `StageClearState_SaveProgress()` = ハイスコア書き出し、`MarkCleared` から自動呼出
- `StageClearState_LoadFromSave()` = ロード + `g_Cleared[]` / `everStarted` 復元 + dissolve キュー破棄
- `StageClearState_DeleteAllProgress()` = `std::remove(kSavePath)` でファイル削除
- `Title_Initialize` で Reset 後にロード → タイトル復帰時もクリア状態維持
- STAGE_3 再入場時に stage_select で 3 ステージ全 CLEAR 表示

### 88. 提出用 アセット整理 (160 → 71 ファイル)
- バックアップ: `*.bak`, `memo.txt`, `*BETA.txt`, `.mayaSwatches/`
- 旧 BGM/SE: `bgm.wav`, `explosion.wav`, `pause.wav`, `title_1.wav` 等
- 旧モデル: `Chara_test_03`, `Circle/Square_Switch*`, `Hip_Hop_Dancing`, `Kabe_1_1_1*`, `Yuka_*`, `Pushing`, `Test_man_*`, `Wooden_Box`, `animation_test_*`, `bennchi`, `lamp`, `tree`, `照明3` 等
- 旧 UI テクスチャ: 各種 Option 旧版 (BGM/Bar/BackTitle/Default/Option/SE/Select*/setumei 等)、Box_Title / KeyPush / Result / Title / Player2D 旧版、ルートの control/jimen/number/player2d/shiro/trail/logo/Title_Arrow/title_logo
- Title_Arrow / title_logo の ロード/解放/draw code も全削除

### 89. 光源カメラ refactor (Player3DCamera と整合)
- `LightCamera_Update` 全面書き直し: 旧 rigid-offset 追従 → ステージ別フォーミュラで光源を中心に追従
  - STAGE_1: `(22, 3.5, lightZ)` → `(6, 3.0, lightZ)` (Z 追従, クランプ 4-20)
  - STAGE_SELECT/STAGE_2: `(lightX, 3.5, -7.5)` → `(lightX, 3.0, 6.5)` (X 追従, クランプ 4.5-8.5)
  - STAGE_3: TPS yaw=180 / pitch=15 / dist=8 で光源を見る
- `Camera_SetLightMode(bool)` 追加: プレイヤーモードでは StageOne/StageSelect Camera が「寄せた値」(StageOne kCamX=18, StageSelect kCamZ=-6) + プレイヤー軸への 0.2 倍トラッキング、ライトモードでは元の wide 値 (StageOne kCamX=22, StageSelect kCamZ=-7.5)
- 「プレイヤー操作時はカメラ少し拡大 + 前後 tracking、光源操作時はフィールド見渡せる wide」

### 90. 光源操作: 左右だけ + ダッシュ対応
- `Switch_Light.cpp`: 入力を X 1 軸のみ、Shift / pad Y でダッシュ (`× 2.2`、ベース 8/60)
- カメラ右ベクトル (`cross(+Y, fwd)`) で投影 → 画面上で常に左右移動
- 旧 yaw ベースの forward/back ロジック撤去
- 移動範囲: 衝突解決後にステージ別ハードクランプ (STAGE_1: x∈[0,14] z∈[2,21]、STAGE_SELECT/2: x∈[0,14] z∈[1,22])

### 91. 箱押しをカメラ相対に
- `Pushing_Obj_Manager.cpp`: WSAD + pad LStick を camera-fwd / camera-right の 2 軸ベクトルに合成、grab 時に決まった軸 (X or Z) へ投影
- 横から掴んだら A/D が、正面/裏から掴んだら W/S が効く。斜めの perpendicular 成分は捨てて 1 軸動作維持
- `proj` をクランプ ±1 して `kPushSpeedPerFrame * proj` で押す

### 92. 3D ジャンプの大幅修正
- `PlayerStatus.h` に `coyoteFrames` (int) 追加
- `Player3D_Gravity`: 接地中は `coyoteFrames = 8`、離地後は毎フレーム --
- `Player3D_Move`: JumpKey トリガー時 `(isGround || coyoteFrames > 0)` の場合のみ JUMP state へ → 空中で押しても着地後再発火しない
- `Player3D_Jump`: ジャンプ実行後・条件不一致時とも必ず `state = PLAYER_STATE_MOVE` へ戻す (latch 防止)。デバッグ出力削除
- ぎりぎりジャンプ (ledge から少し離れた後) が緩和

### 93. Player2D ジャンプ簡略化 + 強制 3D 解除
- coyote / jump-buffer / `g_JumpKeyReleased` ラッチ削除、地面トリガーで初速→ホールド加算→離したらカットの単純構造に
- `Player2D_InitAt`: `g_IsJumping=false / g_JumpHoldTime=0 / isGround=true` を入る時に強制初期化 → 2D 入直後すぐジャンプ可能
- `Player2D_Respawn`: 「1.5 秒接地なしで強制 3D 復帰」削除。落下 Y<-10 強制 3D だけ残存 → 通常ジャンプで誤発火しない
- `Player2DCamera_Update`: 初回フレームは lerp スキップで targetAt/Pos を直接代入 → 前モード残骸からの長 lerp 消滅

### 94. R リセット機能削除
- `Player2D.cpp` の `IsInputTrigger(ResetKey)` ブロック削除。プレイヤー死亡時の自動 Respawn のみ残存

### 95. Title Start を 1 秒待機 → フェードへ簡略化
- 旧 turn + walk + stop + fade の 5 秒シネマティック削除
- `TitleAction_Start`: その場停止 (blockMovement=true、velocity=0)
- `TitleAction_Update`: 60 フレーム経過後に `SetFade(60, FADE_OUT, SCENE_GAME)` → 必ず 1 秒で切替

### 96. ステージ入場時の霧 + プレイヤー fade-in 演出
- ミスト密度倍増 (`stage_clear_state.cpp`):
  - kSparkleCount 140 → 280、core 96 → 180
  - kSparkleRadiusMax 1.3 → 2.6 (中央覆い尽くす)
  - kSparkleSize 0.7 → 1.1、Outer 1.0 → 1.6
  - kSparkleDoorYOffset 1.0 → 0.3 (ドア足元へ低く)
  - kSparkleHeightSpread 1.6 → 2.2
- 入場ミスト位置を `doorPos + 1.5Y` → `doorPos` に (player3D.cpp)
- `iris.cpp` `Iris_GetPlayerAlpha()` 公開:
  - Shrinking 70% 時点で α=0
  - FullClose / BlackHold: 0
  - 新シーン FADE_IN 中: 0 → 1 (`1 - GetFadeAlpha()` 同期)
- `fade.cpp` `GetFadeAlpha()` 追加 (iris から参照)
- `Player3D_Draw` で `Iris_GetPlayerAlpha() < 0.10f` 描画スキップ → 霧へ溶けて再出現

### 97. クリア演出: 主人公が霧から現れる (出口側 fade-in)
- `StageClearCinematic_GetPlayerAlpha()` 公開
- Phase 2 開始から 0..0.45s = 0 (隠蔽)、0.45..0.85s = 0→1 フェード、それ以降 1.0
- ミスト (Phase 2 開始時に発火、0..0.7s でピーク) と同期: 霧が濃い間は player 非表示、霧が薄れると現れる
- `Player3D_Draw` で同じ < 0.10 スキップに統合

### 98. ステージ 1 マンホール (上下する床) 削除
- `stage1.txt` Y=5 layer 中央の `M` セルを `.` に → 上空浮遊プラットフォーム消滅

### 99. Pause 画面の Option 画像振り分け (TITLE/PAUSE 入替)
- ユーザ報告に応じて explain / volume ともに「TITLE = データ消去白 (active)、PAUSE = データ消去灰 (disabled)」で統一
- 「ゲーム中のオプションはデータ消去が灰色」要件を満たすよう volume_pause.png を生成 (Python で領域暗化)

### 78. FIELD_WALL / FIELD_GROUND の Greedy Merge (drawcall 削減)
**問題**: STAGE_1 で wall 767 + ground 380 = 1147 cell、各 1 FBX draw call。main pass 1147 draws/frame。`MergeContiguousField` 導入前は cubemap caching だけでは 30 FPS から脱出できず。

**修正** (`field.cpp`):
- 新規 `MergeContiguousField(FIELD targetType)` 関数: 1x1x1 / 回転なし / カスタムコライダなし のセル群を greedy 1D merge
  - Z 方向に extend → Z run のみなら X、X も 1 なら Y へ
  - 連続セルを 1 つの MAPDATA に置換 (`scale.{x|y|z} = run length`、center = midpoint)
  - 例: STAGE_1 Y=1 layer の col 0 縦壁 24 cells → scale.z=24 の 1 block
  - 例: top row 18W → scale.x=18 の 1 block
- `LoadMapFromFile` 末尾で `FIELD_WALL` と `FIELD_GROUND` 両方を merge (title scene のみ skip)
- 推定 STAGE_1: wall 767 → ~39 blocks (95% 削減)、ground 380 → 1-5 blocks (98%+ 削減)
- 衝突 (`Field_GetCollisionHalfSize`) と camera collision はすでに scale 対応なので追加修正不要
- 視覚: FBX テクスチャが伸びる (ユーザ「ループさせる方法も有り」と allow 済)。将来必要なら専用 tiled-wall path で対応可

### 77. シャドウキューブマップ caching で STAGE FPS 60 化
**問題**: STAGE_1/2/3 で 30 FPS 前後。`GameShadow_RenderCubemap` が毎フレーム 6 face 全 map object 描画 (STAGE_3 で数千 draw call)。

**修正** (`game_shadow.cpp` / `game_shadow.h`):
- 新規 `g_CubemapDirty` / `g_PrevCasterPos` / `g_PrevCasterRot` / `g_LastShadowLightPos`
- 新規 `GameShadow_MarkCubemapDirty()` API 公開
- `GameShadow_Update` で dirty 判定:
  - **光源 (FIELD_OBJ_3) 移動**: `(g_LightPos - g_LastShadowLightPos)^2 > 0.00001` (≈3mm)
  - **シャドウキャスター (OBJ_BOX / OBJ_2 / SEESAW_1/2 / MANHOLE) の pos / rotate 変化**: 毎フレーム snapshot を memcmp で前回比較
  - **`StageClearCinematic_IsActive()`**: ドア Y-scale アニメ中は毎フレーム再構築
- `GameShadow_RenderCubemap` 冒頭で `if (!g_CubemapDirty) return;` → 6 face pass を完全スキップ
- 静的シーン (光源も箱も seesaw も動かない) では cubemap 再構築 0、SRV は前フレームのまま bound → 巨大な perf 改善
- 動的シーン (TAB 光源操作 / 箱押し / seesaw 揺れ / cinematic) では従来通り毎フレーム再構築

## 残タスク (現時点)

### 解決済み (セッション 5)
- ~~`Option.cpp:370` C4244 警告~~ → `40.0f` → `40` 修正 (上記セクション 17)
- ~~`Mouse_PeekState` の grep 漏れ確認~~ → `Polygon3D_Update` は dead code、他の consumer は canonical 3 箇所のみで OK (上記セクション 17)
- ~~merge残骸ファイル削除~~ → commit `7b71013` で完了済 (記載のみ古かった)

### 解決済み (セッション 6〜14)
- ~~タイトル磨き (クリックプロンプト + ロゴ breathe)~~ → セクション 18
- ~~ステージセレクト夜化 (カメラ角度 + 月 + dark sky + bright moon)~~ → セクション 19-21
- ~~壁 4 倍層 + スポットライト shader~~ → セクション 22-24
- ~~追従カメラ + 再入場 + 演出簡略化~~ → セクション 25-28
- ~~フェード中カメラ凍結 (cineActive bug)~~ → セクション 29
- ~~Push/ModeSwitch billboard 点滅 (state 継承)~~ → セクション 30
- ~~iris-out 演出第一弾 + ステージ入場時のカメラ突然回転隠蔽~~ → セクション 31-34
- ~~プレイヤー浮き (R マーカー Y オフセット)~~ → セクション 35
- ~~マウス look + ホイールズーム削除~~ → セクション 36
- ~~STAGE_1/2 を stage_select カメラに dispatch~~ → セクション 37
- ~~Shift+jump 後 dash モーション永続化~~ → セクション 38
- ~~iris 中心 + 速度 + linear ease~~ → セクション 39-40
- ~~**2D スプライト点滅 ROOT CAUSE** (boneWeight 未初期化)~~ → セクション 41 ⭐
- ~~iris 円が完全に閉じない (center 1 pixel 残り)~~ → セクション 42
- ~~CLEAR billboard アスペクト~~ → セクション 43
- ~~Option デフォルト (mouse sens, drag 状態残留)~~ → セクション 44
- ~~Esc + Explan 動作~~ → セクション 45
- ~~STAGE3 光源操作の重さ~~ → セクション 46
- ~~クリアドア正面フレーミング~~ → セクション 47
- ~~ステージドア 2 倍スケール~~ → セクション 48
- ~~iris 完全閉鎖 + FullClose hold~~ → セクション 49
- ~~BGM 1.5 秒クロスフェード~~ → セクション 50
- ~~ステージライト真下スポット + 全体暗化~~ → セクション 51
- ~~Phase 2 主人公出現演出復活~~ → セクション 52

### 解決済み (セッション 4)
- ~~ステージクリア後 / リスポーン時の dash 継続~~ → `Player3D_InitAt` に dash クリア追加 (上記セクション 14)
- ~~ステージ開始時のカメラ挙動がおかしい~~ → `Player3DCamera_Reset` + 初回 snap で構図を即座に決定 (上記セクション 15)
- ~~STAGE_SELECT が TPS 追従~~ → `StageSelect_Camera_Update` 固定ポーズに切替 (上記セクション 15)
- ~~霧エフェクトが広がりすぎ・粗い~~ → 半径縮小 + パフ数増 + サイズ縮小で煙感アップ (上記セクション 16)

### 解決済み (セッション 2)
- ~~マウス操作が効かずカメラが動かない~~ → `Mouse_PeekState` API 追加で根本解決 (上記セクション 9)
- ~~Option 画面のフレーム点滅~~ → `Option_Draw` の二重判定除去で解決 (上記セクション 10)
- ~~フェード中の全入力ロック~~ → `Input_IsGloballyLocked()` で gate (上記セクション 8)
- ~~ステージクリア演出の仕様変更 (次ドア霧出現)~~ → 5 フェーズ化 + Phase 4 reveal で実装 (上記セクション 11)

### 解決済み (セッション 3)
- ~~Option UI 当たり判定とボタン視覚が乖離~~ → window-client → back-buffer スケール + rect 寸法を bbox 実測で再配置 (上記セクション 12)
- ~~Option を開くと点滅~~ → BG テクスチャが alpha=127 だったので不透明 underlay を前に描画 (上記セクション 12)
- ~~クリア演出 次ドアが横 90° 表示~~ → `ComputeDoorFramingFront` でドア yaw からの前方向 framing に切替 (上記セクション 13)
- ~~霧が薄い / リアルじゃない~~ → 2 レイヤー 96 puffs + smoothstep alpha 0.96 + 上昇ドリフト (上記セクション 13)

### 次セッション タスク (2026-06-08 ユーザ指定)

1. ~~**クリア演出フロー再設計**~~ → セッション 20 セクション 76 で完了
2. ~~**TAB 光源操作カメラの乖離修正**~~ → セッション 20 セクション 74-75 で完了
3. ~~**STAGE 内 FPS 60 確保**~~ → セッション 20 セクション 77 で完了 (cubemap caching)

4. **STAGE_2 / STAGE_3 / STAGE_1 のマップ全面作り直し** (ユーザ側で実施予定)
   - 確定後、game_shadow のフィルタ条件 (現状 STAGE_1 のみ n.x>0.7) を各 stage の light 方向に合わせて拡張
   - StageOne_Camera と同様の per-stage カメラを追加するか、generic な「light の方向から見る」カメラに抽象化

5. **過去セッションからの追跡項目**:
- 5 フェーズ cinematic が STAGE_2 クリア時 (次 = STAGE_3) で期待通り動くか実機確認
- Phase 4 で次ドアの足元位置 (`mapData.pos.y`) からの Y-scale 0→1 がドア arch モデルの軸と合っているか (`field.cpp` の `XMMatrixScaling` は `g_MapData[i].scale.y * doorScaleY`、Translation はそのまま → Y=0 のときドアが床下に沈むかどうかは要確認)
- 新カメラ距離 (5.2 back, 2.0 height, 1.4 lookAt) でフレームが切れないか実機チェック
- 140 puffs の描画コスト: ドロップフレームが起きるなら kSparkleCount を絞る (現状 GAME_GOAL hit のみで発火する短時間効果なので問題ないと推測)
- `StageSelect_Camera_Update` の固定ポーズ `(6.0, 7.5, -4.0)→(6.0, 1.5, 5.5)` が実機 1280×720 / 1980×1080 両方で3ドア+プレイヤーをきちんと収めているか
- ステージ開始時のカメラ snap で初フレームのマウスデルタを捨てている (gCamAnglesInit ガード) → 影響範囲は次フレームから通常 lerp に復帰するので問題なしのはずだが念のため確認

### 既存残タスク
| タスク# | 内容 | 状態 |
|---|---|---|
| 9 | シーソー影 collision 検証 (新 AABB sweep 後の挙動確認) | ユーザー判定: ほぼ問題なし、現状維持 |
| — | 影 polygon が壁端で欠けるエッジケースの追検証 | 未 |
| — | ステージパズル化リデザイン (`stage1/2/3.txt`) | 未 |
| — | UI / 演出のさらなる磨き (HUD, リザルト) | 未 |
| — | game.cpp の責務分離リファクタ | 未 |
| — | セーブ機能 (クリア進捗永続化) | 未着手 (`StageClearState_DeleteAllProgress` API は用意済) |
| — | ~~Option 画面の全体作り直し~~ | セッション 16 で完了 |
| — | ~~ポーズ画面: 4 ボタン~~ | セッション 19 で 2 段階分離に置換 (pause_bg → クリックで Option) |
| — | タイトル画面リデザイン: WASD + push + 影変身可能、マウスカーソル / LR で「オプション / ゲームスタート / ゲームをやめる」 | 未着手 |
| — | ~~タイトル限定 Option に「進捗リセット」ボタン + YES/NO 確認ダイアログ~~ | セッション 16 で完了 |
| — | ~~クリア演出フロー再設計~~ | セッション 20 セクション 76 で完了 |
| — | ~~TAB 光源カメラ乖離修正~~ | セッション 20 セクション 74-75 で完了 |
| — | ~~STAGE 内 FPS 60 化~~ | セッション 20 セクション 77 で完了 (cubemap caching) |
| — | ~~FIELD_WALL/GROUND drawcall 削減~~ | セッション 21 セクション 78 で完了 (greedy merge + tiled wall) |
| — | ~~タイトル画面リデザイン (Title_Menu.png 3 ボタン)~~ | セッション 21 セクション 81 で完了 |
| — | ~~Pause 5 ボタン化 (Resume/ReStart/Option/Stage Select/Stage Exit)~~ | セッション 27 セクション 83 で完了 |
| — | ~~ボタンクリック SE + Switch アイコン~~ | セッション 23-24 セクション 84-85 で完了 |
| — | ~~Game Clear (STAGE_3 後) + 進捗セーブ~~ | セッション 25 セクション 86-87 で完了 |
| — | ~~アセット整理 (提出用 160→71)~~ | セッション 26 セクション 88 で完了 |
| — | ~~光源カメラ refactor + 左右だけ + ダッシュ~~ | セッション 28 セクション 89-90 で完了 |
| — | ~~3D / 2D ジャンプ修正 (coyote + latch 修正)~~ | セッション 28-29 セクション 92-93 で完了 |
| — | ~~ステージ入場/退場の霧 + プレイヤー fade~~ | セッション 29 セクション 96-97 で完了 |
| — | ~~進捗セーブ (save.dat)~~ | セッション 25 セクション 87 で完了 |
| — | ~~Title/Pause/Option 全 UI 1920×1080 統一~~ | セッション 24 セクション 80 で完了 |

### 次セッション要対応
- **3D 床上でジャンプできない件 (継続)**: セッション 29 でロジックは修正済 (`(isGround || coyote>0)`) だがユーザ報告で再発。要 ImGui 経由の isGround / state / coyoteFrames 実時間ログ仕込みで原因特定
- **左右壁の手前延長**: stage1.txt のマップ幅 (18 列) 越え拡張。要マップ作り直し or 列追加
- **STAGE_2 / STAGE_3 のマップ作り直し**: ユーザ側で実施予定。完成後 `game_shadow.cpp` の影フィルタ条件 (`STAGE_1: n.x>0.7`) と `LightCamera_Update` の per-stage 値を各 stage の光方向に合わせて拡張
- **タイトル限定 Option の「進捗リセット」確認**: セクション 56 で実装済だが、セーブ実装 (セクション 87) と統合確認したいユーザ動作テスト

## 動作確認テンプレ
新しい変更をユーザに渡した後の確認項目:
1. Visual Studio でビルドが通るか (現在: エラー0、警告のみ)
2. **タイトル**: 左クリックで開始、Enter は無効
3. **ステージ選択**:
   - 初回起動時 STAGE1 のドアと easy 画像のみ表示、STAGE2/3 は完全非表示
   - F 連打で影モードにならないこと、フェード中もプロンプト出ないこと
   - F 押下時のみステージ入場、体当たりでは入らない
4. **ステージクリア**:
   - 各クリア → stage_select 復帰 → カメラがクリアしたドアにフォーカス → 黒い霧でドア消失 → CLEAR 画像出現 → カメラがプレイヤーに戻る (3.5秒)
   - ドアが復活しない (霧後ずっと消えたまま、影も出ない)
   - 次のステージのドア+画像が出現する
5. **STAGE3 クリア**: stage_select に戻らず直接 RESULT へ
6. **影モード操作**:
   - 影の壁・天面で押し戻しが起きない、自然に滑る
   - 壁のない空間に飛び込むと約 1.5秒で 3D モードに自動復帰、ボックス位置もリセット
7. **3D モード操作**:
   - Shift ダッシュ中に落下 → リスポーン後 Shift 離してれば歩き速度
   - 左クリックで箱掴み、W/S で押し引き (横キー無視、斜め禁止、4方向のみ)
   - F キーは変身 (Change) 専用
8. **TAB 光源操作**: 切替も戻りも滑らかな lerp (両方向ともワープしない)
9. **ポーズ (Esc)**:
   - カーソル表示
   - UI / 背景に点滅なし
   - 再度 Esc → カメラ mouse-look が即復活
   - 各ボタン (操作説明 / リセット / タイトル戻る) の見た目とクリック判定が一致
   - スライダー (Master/BGM/感度) を上下にウネウネしながらドラッグしても値が一貫追従
10. **音量**: 起動時に小さく、バー操作で適切にスケール

## 編集時のチェックリスト
- [ ] 編集前にファイルのエンコーディングを Python で確認 (`cp932` で decode 通るか、UTF-8 なら明示)
- [ ] CP932 ファイルの既存日本語コメント部分は Edit ツールを使わず Python バイト編集
- [ ] 新規追加コメントは **ASCII のみ** (em-dash・全角句読点を Python 文字列に混ぜない → C4819 警告再発)
- [ ] 編集後に encoding (cp932/utf-8) と 改行 (CRLF) が保持されているか確認
- [ ] `git diff` で意図通りの変更か目視確認

## 既知の構造的課題 (未着手)
- `game.cpp` の責務肥大 (モード分岐・影生成・カメラ・BGM切替・billboard 描画・シャドウキューブマップ全部)
- グローバル変数の多用 (`newKeyBind.cpp`, `PlayerModeSwitchManager.cpp`, `Pushing_Obj_Manager.cpp` の static 多数)
- 静的初期化順序のリスク (`PlayerModeSwitchManager.cpp` 冒頭の `PLAYER* p = GetPlayer3D();`)
- モデル差し替えは不可 (ユーザ指示) — Player/ball/sky の FBX はそのまま使う
- `ActionKey` / `ChangeKey` の F + B 完全重複 (push 分離で実害減ったが残置)
- 影 polygon の精度は AABB スイープに頼っているため、傾いた壁面では bounding box がやや loose

## 主要モジュール (セッション 7 以降で新規追加)
- `iris.cpp/.h`: iris-out 遷移 (Idle → Shrinking → FullClose → BlackHold)。`Iris_Start / Update / ApplyToShader / IsActive / StartBgmFadeOut`
- shader `b6` cbuffer: `IrisCenter / IrisRadius / IrisActive` (back-buffer pixel 座標で円外を黒に)
- shader Light cbuffer `Dir.w >= 0.5`: spotlight mode 有効化 + cone falloff
- `field.cpp` `Field_DrawLightBalls()`: FIELD_OBJ_3 ボールを unlit / 全明描画 (月 / 光源の存在感)
- `camera.cpp` `StageSelect_Camera_Reset()`: 追従 X 状態リセット (stage 切替時に Game_Initialize から呼出)
- `stage_clear_state.cpp` Phase 2 player emerge ロジック: 主人公をクリアドアにテレポートして walk → idle

## デバッグ用メモ
- 2D スプライト点滅は**必ず boneWeight 初期化**を疑う (`Map(WRITE_DISCARD)` の戻り値はゴミ)
- フェード関連の挙動異常は `cineActive` に `fadeLocked` が混入していないか確認 (カメラは fade 中も更新が必要)
- iris 不完全閉鎖は shader の `dist >= IrisRadius` (`>` ではない) と FullClose hold の両方を確認
- BGM 不連続は `Game_Finalize` の `UnloadAudio` 前に `FadeOutAndStopAudio` が走っているか確認
- STAGE3 perf はシャドウキューブマップパスの `FIELD_GROUND / FIELD_EMPTY_BOX` skip が効いているか
