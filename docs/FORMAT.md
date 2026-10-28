# PhiPhysiceEditorNeo 文件格式

## 1. 原件：`components/`

### 1.1 语义

`components/` 分为 `physics/`（物理块）、`scene/`（场景块）和 `fields/`（力场）三个文件夹。物理块与场景块共用同一套 `.note` 节点格式，但各自从自己的文件夹加载；放置到画布上的实例只保存类型名称，节点和材质在运行时从对应文件解析。程序不内置 note 节点默认值，也不会在启动/载入时创建或覆盖 `.note` 文件；节点始终以存放的文件为准。修改文件后重启或重新保存类型即可刷新。

### 1.2 语法

文件是 UTF-8 纯文本，一行一个字段。空白行和 `#` 注释会被忽略。

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `name <string>` | 是 | 类型名称，与文件名（不含 `.note`）一致；使用 ASCII 字母、数字、`_`、`-`，首字符为字母 |
| `vertices <int>` | 建议 | 顶点数量；用于校验后续 `v` 行数量 |
| `v <x> <y>` | 是 | 一个顶点，设计像素坐标；重复 `vertices` 指定的次数 |
| `density <float>` | 否 | 密度，默认 1.0，范围 0.05–100 |
| `friction <float>` | 否 | 摩擦系数，默认 0.45，范围 0–2 |
| `restitution <float>` | 否 | 弹性，默认 0.08，范围 0–1 |
| `color <r> <g> <b>` | 否 | 0–255 颜色，默认 96 165 250 |
| `yaxis up` | 否 | 声明文件使用 Y-up 坐标；缺失且文件头为 v1 时按旧 Y-down 载入并迁移 |
| `&<name> <display> [= <value>]` | 否 | 声明一个变量；显示名可含空格，默认值可省略（0） |
| `$<vertex> <xExpr> <yExpr>` | 否 | 节点坐标覆盖绑定；`this` 为该节点的默认坐标，需要偏移时写 `this + ...`；只写一个表达式时表示 Y 坐标，也支持 `x=... y=...` 形式 |

`count` 和 `vertexcount` 是 `vertices` 的兼容别名；`vertex` / `vert` 是 `v` 的别名。

### 1.2.1 四种音符

编辑器提供 **Tap、Drag、Flick、Hold** 四种音符；`physics/` 和 `scene/` 文件夹各自保存一套。名称不是这四者的 `.note` 文件会作为铆钉 / 判定线等原件配置被忽略，不会出现在音符列表中。

### 1.2.2 变量默认值与每个音符的数值

`&变量名 显示名 = 默认值` 声明的是**类型默认值**。放置音符后，选中它即可在检查器的“变量”区域单独修改数值；该值只影响当前音符，并写入工程的 `[notevars]` 段。没有单独覆盖的变量继续使用类型默认值。节点绑定在每次渲染/重建物理时按最终数值求值。

### 1.3 示例

```text
# PhiPhysiceEditorNeo shape v2
yaxis up
name Flick
vertices 12
v -95 12
v -6 12
v -6 24
v 14 12
v 95 12
v 100 0
v 95 -12
v 6 -12
v 6 -24
v -14 -12
v -95 -12
v -100 0
density 1.15
friction 0.35
restitution 0.18
color 244 114 182
```

带变量和节点绑定的 Hold 示例：

```text
# PhiPhysiceEditorNeo shape v2
yaxis up
name Hold
vertices 8
v -100 18
v -100 10
v -95 0
v 95 0
v 100 10
v 100 18
v 95 28
v -95 28
color 140 232 194
&height 高度 = 0
&width 宽度 = 200
$0 this this+height
$5 this this+height
$6 this this+height
$7 this this+height
```

- Hold 的原点在底部中心：0 / 5 / 6 / 7 是上半部分节点，`height` 只让它们向上移动；底部 1 / 2 / 3 / 4 节点保持不动，面积重心随高度变化。
- 变量名是 ASCII 标识符，供表达式引用；显示名用于编辑界面。
- 表达式支持 `+ - * / % ^`、括号、变量、常量 `pi`/`e`，以及 `abs/sqrt/sin/cos/tan/floor/ceil/round/min/max/clamp`。
- 绑定结果覆盖该节点的 X/Y 坐标；`this` 是该节点在 `v` 行中的默认坐标，节点编号从 0 开始，对应 `v` 行顺序。多个节点重合时仍按 `v` 行保留，不会自动合并。

### 1.4 载入规则

1. 识别旧版 Y-down 文件并把顶点 Y 取反；
2. 去除连续重复顶点和共线顶点；
3. 若顶点是负向绕序则反转；
4. 保留文件中的 (0,0) 作为音符原点，不自动平移到质心；物理构建时再单独计算面积质心作为重心；
5. 校验面积、自相交、顶点数（3–64）；
6. 校验变量名、表达式和节点编号；
7. 材质字段夹紧到合法范围。

保存时会把归一化后的顶点重新写回文件。形状编辑器中的“面积”和“凸块数量”来自同一套几何逻辑。

### 1.5 错误处理

- 文件无法解析时不会影响其他类型；错误显示在左侧原件库上方，鼠标悬停可查看第一条错误；
- 文件名与 `name` 不一致的文件会被忽略；
- 同名类型保留一个；
- 工程引用了已删除的类型时，画布显示“缺失类型”警告，物理世界跳过该音符。
- 铆钉和判定线也使用类似格式：`physics/Rivet.note`、`scene/Rivet.note` 保存 `kind rivet`、`radius`、`color`；`scene/JudgeLine.note` 保存 `kind judge`、`width`、`height`、`color`。这些文件不会被识别为音符类型。
- `fields/<name>.note` 保存力场默认参数：`kind`、`name`、`shape`、`width`、`height`、`radius`、`strength`、`buoyancy`、`damping`、`vortex`、`dirx`、`diry`。放置新力场时使用对应文件的默认值；`vortex` 只用于涡流场，正数顺时针、负数逆时针。

## 2. 工程：`projects/<name>.pnp`

### 2.1 语法

```text
PHIPHYSICE-PROJECT 17
name <string>
gravity <m/s²>
iterations <velocity> <position>
slowstep <0|5|10|...|50>
eventsplit <1..64>
nextids <nextNote> <nextJudge> <nextJoint> <nextGroup> <nextChain> <nextField>
[notes]
note <id> <type> <x> <y> <angleDeg> <group> <gravity> <scene> <radius> <r> <g> <b> <ghost>
[notevars]
notevar <id> <name> <value>
[judges]
judge <id> <x> <y> <angleDeg> <width> <height> <r> <g> <b> <ghost>
[fields]
field <id> <kind> <shape> <x> <y> <angleDeg> <width> <height> <radius> <strength> <dirX> <dirY> <buoyancy> <damping> <vortex>
[rev]
rev <id> <noteA> <noteB> <localAx> <localAy> <localBx> <localBy> <refDeg> <collide> <chain>
[weld]
weld <id> <noteA> <noteB> <localAx> <localAy> <localBx> <localBy> <localA2x> <localA2y> <localB2x> <localB2y> <group>
[events]
note|judge|field <ownerId> <track> <type> <startBeat> <endBeat> <easing> <startExpr> <endExpr> [instant] [angleExpr]
[end]
```

- 所有长度/坐标单位为**设计像素**，Y 轴向上为正；文件中角度为**度**，内存中为弧度。旋转角度默认 0，正数顺时针、负数逆时针；方向类角度（浮力、引力改变等）以 0 为正上方。
- 音符位置是它的文件原点（Hold 默认在底部中心），判定线位置是中心；物理重心由音符轮廓的面积质心决定，与原点相互独立。
- `[judges]` 是判定线（旧版“静态平台”），默认 4000 × 5 px、淡黄色 `255 238 150`；它通常作为静态刚体参与碰撞。`ghost` 为 1 时判定线保持原色但绘制为虚线，并且不再参与碰撞。
- `[notevars]` 保存每个音符的变量覆盖值；没有出现的变量使用 `.note` 中的默认值。旧版 `note` 行末的 Hold 高度字段会被忽略。
- 版本 1 的 `[floors]` 段仍可读取，会被转换成判定线；引用了不支持类型（如 Bar、Star）的音符会被跳过，相关关节一并丢弃。
- 版本 3 起使用 Y-up，版本 4 增加 `[notevars]`，版本 5 增加每个音符的重力方向，版本 6 增加 `[fields]` 力场，版本 7 增加场景块标记，版本 8 增加力场阻尼和阻力/引力改变两种力场，版本 9 增加缓步开关，版本 10 把缓步改为 0–50 挡位，并为 `note` 行增加铆钉半径和颜色，版本 11 增加 `[events]` 事件时间轴，版本 12/13 调整用户角度约定（当前：默认旋转 0，正数顺时针；方向 0 为正上方），版本 14 增加重力方向事件类型，版本 15 增加方位/旋转动量事件和每个事件的瞬时完成开关，版本 16 在 `note` 和 `judge` 行末增加 `ghost` 虚化状态并增加虚化事件类型，版本 17 为 `[fields]` 增加有符号涡流强度和“涡流强度”事件。载入 v1/v2 时会把音符/判定线的 Y 与角度、关节局部锚点及参考角在内存中翻转一次；载入过程只读，不会自动改写原文件。
- **文件头兼容**：1.0 起写入 `PHIPHYSICE-PROJECT`，但仍接受旧版 `POLYNOTE-PROJECT`；保存时统一写新头。
- `[fields]` 保存固定在世界中的力场：`kind` 0 引力、1 斥力、2 浮力、3 阻力、4 引力改变、5 涡流；`shape` 0 矩形、1 圆形。矩形使用 `width`/`height`，圆形使用 `radius`。`strength` 是引力/斥力在中心的最大加速度（m/s²；阻力场中作为阻力系数），`dirX`/`dirY` 是浮力/引力改变场的方向；浮力场的 `buoyancy` 是流体密度，按 `F = 流体密度 × 排水面积 × 重力` 计算，并叠加 `0.5 × 流体密度 × Cd × 排水面积 × |v| × v` 的流体阻力。`damping` 是引力场阻尼，0 表示关闭；版本 17 起末尾的 `vortex` 是涡流场在中心的最大切向加速度，正数顺时针、负数逆时针，向边缘衰减到零。版本 7–9 的拟真浮力开关和浮力底部阻力仍会被读取并忽略。
- 新放置力场统一下落为矩形，默认尺寸 100 × 100；放置后可在检查器中改为圆形（默认半径 50）。
- `gravity` 字段（`note` 行末）取值为 0–8：0 上、1 下、2 左、3 右、4 左上、5 右上、6 左下、7 右下、8 无。缺省值为 1（下），此时音符与场景重力同向；选择“无”时该音符的重力加速度为零。
- `note` 行末的 `scene` 为 1 时是场景块：顶点仍来自同名 `.note` 文件的默认值，但刚体是静态的，固定在世界中（包括 Hold）。
- `note` 行末的 `radius r g b` 对 `Rivet` 类型有效，分别保存铆钉半径与颜色；普通音符仍从 `.note` 文件读取颜色，这些字段被忽略。版本 16 起 `note` 行末的 `ghost` 为 1 时，该 Tap / Drag / Flick / Hold（物理块或场景块）半透明绘制并且不参与碰撞；铆钉忽略该字段。
- `noteA = -1` 的 `rev` 表示世界锚点，此时 `localA` 是世界坐标。
- `collide` 为 1 表示铰链两端仍参与碰撞；默认 0，重建世界时同一链的刚体使用负 group 互相忽略碰撞。
- `weld` 的两个锚点共同锁定相对位置与旋转。
- `nextids` 用于保存后继续分配不冲突的 id；载入时也会根据最大 id 自动修正。
- `gravity` 与 `iterations` 是场景物理设置（重力加速度、速度/位置迭代次数），缺省为 9.8 / 10 / 4。
- `slowstep` 为缓步挡位：0 关闭，5–50 把物理与力场力度除以挡位、播放速度乘以挡位，并按挡位细分碰撞子步。挡位越高越能减少穿模，性能消耗也越大；新工程默认 10。
- `eventsplit` 是事件时间轴的每 beat 切分数，默认 4（四分之一 beat 吸附），范围 1–64。
- `[events]` 中 `track` 为 0–9（从左到右），`type` 为 0 X移动、1 Y移动、2 旋转、3 宽度、4 高度、5 半径、6 强度、7 流体密度、8 阻尼、9 方向角度、10 重力方向、11 方位动量施加、12 旋转动量施加、13 虚化。`gravity_direction` 只用于物理块中的普通音符和物理铆钉，取值为 0–8，与 `note` 行的重力方向编号一致；插值结果按四舍五入取最近的方向，场景块等静态原件即使文件里出现该事件也会被忽略。`directional_momentum` 用于物理音符和物理铆钉，`rotational_momentum` 只用于普通物理音符；事件值表示在持续时间内分摊的总动量，`instant = 1` 时改为在起点一次施加。方位动量的 `angleExpr` 为方向表达式（度，0 为正上方、正数顺时针），其 `this` 为音符当前角度；旋转动量正数顺时针。`instant` 对每种事件都生效：开启后忽略时长与缓动，在起点直接到达结束状态；虚化事件（13）固定为瞬时，结束值 ≥ 0.5 开启虚化，< 0.5 恢复实体。`easing` 为 1–28，对应内置的 28 种标准缓动（线性、正弦、二次、三次、四次、五次、指数、圆形、回弹、弹性、弹跳），在事件面板中直接输入编号修改。表达式已去除空白，支持 `this`、`pi`、`t`、`et`。
- 工程保存的是**编辑布局**的初始位置。播放/单步产生的位置只存在于当前物理世界，不写回文档，除非用户先重置再修改。

### 2.2 校验

载入时校验：

- 文件头与版本（当前为 15，兼容 1–14）；
- 数值字段是否可以解析、是否为有限数；
- 铰链/焊接引用的音符是否存在；
- 截面/宽高不小于最小值。

保存采用临时文件 + 重命名。

## 3. 设置：`settings.ini`

设置文件位于程序数据根目录，首次修改设置时创建，之后立即写入：

```text
PHIPHYSICE-SETTINGS 1
light 0
accent 0
show_grid 1
show_joints 1
show_labels 1
show_wireframe 0
show_contacts 0
show_grid_labels 1
gravity 9.8
velocity_iterations 10
position_iterations 4
slow_step_gear 10
rivet_radius 15
export_directory exports
[shortcuts]
play_pause 32 0 0
reset_simulation 82 0 0
...
```

- `light`：0 夜间模式，1 日间模式；启动时自动载入。
- `accent`：主题色编号 0–7（蓝、粉、绿、黄、橙、天蓝、紫、灰）。
- `show_*`：设置“基础”页中的显示开关，启动时恢复；缺失时使用默认值。
- `gravity` / `velocity_iterations` / `position_iterations` / `slow_step_gear`：设置“基础”页中的场景参数和缓步挡位。
- `rivet_radius`：原件库底部的全局铆钉半径。
- `export_directory`：预制事件导出目录；空值或缺失时使用程序目录下的 `exports`。
- `[shortcuts]`：`动作 id 按键码 Ctrl Shift`；按键码为 raylib 的键值，Ctrl/Shift 为 0 或 1。
- 缺失或无法读取的文件使用默认设置；未知动作 id 会被忽略。

## 4. 数据目录与跨平台

程序在以下位置寻找数据根：

1. 可执行文件所在目录（及其向上最多 5 级父目录）；
2. 当前工作目录（及其父目录）；
3. 要求同时存在 `components/` 和 `assets/`。

因此：

- 根目录的 `PhiPhysiceEditorNeo.exe` 使用根目录的 `components/`、`projects/`、`assets/`，设置写入根目录的 `settings.ini`；
- `build/x64/PhiPhysiceEditorNeo.exe` 使用构建时复制到该目录的同一套数据，设置写入 `build/x64/settings.ini`；
- 整个 `PhiPhysiceEditorNeo/` 文件夹可以整体拷贝到 Windows / Linux / macOS，数据相对路径不变。

所有路径通过 `std::filesystem` 处理；字体通过内存读取，避免 raylib 的窄字符路径在中文目录下出问题。