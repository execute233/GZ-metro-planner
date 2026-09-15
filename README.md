# 广州地铁 · 终端地图规划

C23 / Windows Terminal / PDCursesMod。地图由 SQLite 查询本地 MBTiles，解压 gzip 后解析 MVT v2，使用 Braille 绘制线路，中文站名单独显示。运行时不需要 Python、Go 或网络。

## 构建与运行

需要 CMake 4.0+、MinGW GCC、Ninja。SQLite 3.50.4、zlib 1.3.1 和固定版本 PDCursesMod 的源码均已随项目保存在 `lib/`，直接编译并静态链接；首次配置也不需要联网下载、Git 或 Python。

```powershell
cmake -S . -B cmake-build-debug -G Ninja
cmake --build cmake-build-debug
ctest --test-dir cmake-build-debug --output-on-failure
./cmake-build-debug/GZ_metro_planner.exe
```

推荐深色 Windows Terminal、支持中文的等宽字体、120×40 或更大窗口。背景与普通文字使用终端默认色，线路对深色背景做亮度调整。

依赖目录为 `lib/sqlite/`、`lib/zlib/`、`lib/pdcursesmod/`。PDCursesMod 固定提交为
`520adbae06981c8eb9c0222bc582b6435329335e`，构建只使用 C 核心和 WinCon 后端。
版本、下载来源、校验值和许可证记录见 [lib/README.md](lib/README.md)。
旧的 `GZMP_PDCURSES_SOURCE_DIR` 配置不再使用；现有构建目录重新运行 CMake 即可切换到仓库内源码。

程序固定读取 EXE 同目录的 `metro.mbtiles`，无需路径参数，从其他工作目录启动也一样。非内嵌构建（包括 Debug）每次构建会将项目根目录的 `metro.mbtiles` 同步到 EXE 目录；构建会覆盖开发副本的修改，需保留的维护数据请先复制回项目根目录。

## 打包单文件分发包

默认数据 `metro.mbtiles` 可编译进 exe，产出一个自包含、可直接发给他人运行的
`GZ_metro_planner.exe`。同目录地图缺失时会把内置地图完整释放到 exe 所在文件夹，已有地图始终保留；若该目录不可写，
程序只报错并退出，不写入别处（地图内按 M 的维护变更也写回同一位置）。

```powershell
cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DGZMP_RELEASE_BUNDLE=ON
cmake --build cmake-build-release --target GZ_metro_planner
```

打包产物可以不带 `data/` 目录，exe 首次运行会在同目录生成 `metro.mbtiles`。

## 操作

| 操作 | 按键 |
| --- | --- |
| 切换地图、起点、终点、规划目标 | Tab / Shift-Tab |
| 开始搜索起点 | 地图中按 `/` |
| 搜索 | 中文、全拼或首字母，如 `体育西路` / `tiyuxilu` / `tyxl` |
| 选择搜索候选 | 上下键、Enter |
| 取消编辑、返回地图 | Esc |
| 切换规划目标 | 聚焦目标后左右键或 Enter |
| 平移地图 | 地图聚焦时 WASD / 方向键 |
| 缩放 | `+` / `-` |
| 全网总览 / 适配路线 | R / F |
| 交换起终点 / 清除路线 | X / C |
| 滚动行程 | PgUp / PgDn |
| 浏览全部线路 | 地图中 L；上下键选择，Enter 查看站点与换乘线路 |
| 滚动线路列表 / 返回 | 上下键、PgUp / PgDn；Esc 详情 → 目录 → 地图 |
| 维护站点和线路 | 地图中 M，Esc 取消并返回 |
| 退出 | 地图中 Q；任意位置 Ctrl+C |

确认起终点后自动规划，改变目标后自动重算。输入文字时不触发地图快捷键。总览只显示轮廓；放大后先显示换乘站标签，再显示普通站名。文字同时避开线路、站点和其他站名，附近没有空位就暂不显示，继续放大即可显示更多标签。SVG 标签方向仅作为优先选择，实际占位按终端字符宽度计算。

换乘站用加粗的单格 `#`，起终点用 `@`。每个逻辑站只有一个标识，SVG 中多条线路的站点锚点会合并到一个中心；同一字符格内起终点优先于换乘站，换乘站优先于普通站。标题有独立一行，不覆盖地图。

90 列以上地图与右栏并排；较窄窗口按焦点切换地图与表单页。最低 50×16。

线路浏览使用全屏列表，返回后保留原路线规划与地图视角。站序按图集中的真实区间整理：分支与断开区段分组展示，环线重复起始站并标明闭合；分岔站可能出现在多个站段中。

## 当前图集及数据边界

- 几何来自 `data/calibration/network-labeled.svg`：以用户的 `data/railway.png`（4096×4096）为底图完成的全网目视描线及独立文字排版。
- 图集初版含 **357 个站点、21 个线路标识、421 个区间**。3 号线分支共用线路标识，14 号线支线独立标识；12 号线和佛山 3 号线保留图中未连接的区段。
- APM、海珠有轨电车和南海有轨电车未在这张图片中展开，当前图集不额外添加它们。
- **每个区间暂定 1000 米、60 秒**。这些不是实际运行数据，暂不包含候车或换乘步行时间。
- SVG 曲线离散化误差阈值为 0.35 像素；区间端点在站点附近平滑接到统一站点中心，中段保持原描线。仍属目视初校数据，局部半径和站点位置可继续精修，不作为官方运营数据。
- 示意坐标映射到虚拟墨卡托范围，仅用于瓦片定位，不能用它计算实际距离。

## 校对与重新生成

当前几何校对入口为 `data/calibration/network-labeled.svg`。应用只读取 SQLite，日常增删请使用地图内维护。直接修改 SQLite 的 `edges.seconds` / `edges.meters` 后重启可更新规划权重；不要修改瓦片二进制来调整权重。

仅离线制图需要 Python。关闭正在运行的地图后执行 SVG 导入，再重启应用：

```powershell
python -m venv .venv-map
./.venv-map/Scripts/python.exe -m pip install --only-binary=:all: -r tools/requirements.txt
./.venv-map/Scripts/python.exe tools/import_svg_map.py
```

导入器使用 MVT v2、4096 extent、0～5 级缩放、gzip 压缩和 MBTiles TMS 行号。当前图集含 513 个瓦片。写入前在 `output/svg-import-backups/` 保存完整 SQLite 备份；坐标、线路颜色、换乘标记、标签方向和全部瓦片在单个事务中更新，失败回滚。站名、拼音、英文名、区间里程和时间保留原数据库值。SVG 与数据库的 ID、站名、连接关系不匹配时拒绝导入，避免丢失维护变更。

支持 M/L/H/V/Q/C/S/T 及相对路径。编辑器产生的 transform、圆弧 A 或多个子路径须先转换为不带变换的单条开放曲线路径；遇到这些结构会明确拒绝，不会忽略。图层及站点/区间 ID 必须保留。最多在端点周围 40 像素内平滑调整连接，确保多线路换乘最终只对应一个站点位置。

`data/source/network.json` 和 `tools/build_mbtiles.py` 保留为历史完整建库流程；它们不会读取新 SVG，运行会覆盖当前校准几何与数据库维护数据。当前地图几何更新请使用 `import_svg_map.py`。

最初的自动提取流程为 `tools/ocr_map.py` → `tools/prepare_network.py --force` → `tools/build_mbtiles.py`。此流程不用于保存本轮 SVG 校准结果。

MBTiles 增加 `stations`、`lines`、`edges` 表存放规划与搜索数据；标准地图软件可以读取其中的标准 `metadata` / `tiles`，本程序则要求 `gzmp_schema=2` 的应用数据。它不是通用的任意 MBTiles 查看器。

SVG 导入额外写入可选 metadata：`svg_source_sha256` 和 `svg_label_side_<站点ID>`，方向 1～8 分别对应 E/W/N/S/NE/NW/SE/SW。它们不改变 schema 2 的业务表；没有方向元数据的图集仍能正常显示。站名不烧入瓦片，由 C 渲染器动态放置。

## 地图内维护

`metro.mbtiles`（SQLite）是应用唯一的数据源。旧独立文字模式及 CSV 数据文件已移除。

在地图焦点下按 **M**，选择添加/删除站点或添加/删除线路。表单中按 Enter 进入下一步，最终输入 `y` 保存，`n` 返回维护菜单；Esc 丢弃尚未保存的表单并回到地图。

- 新站点填写名称、全拼、首字母、可选英文名和 0–4096 范围的示意图坐标。面板显示进入维护前的地图中心坐标供参考。
- 新线路填写名称、六位 RGB 颜色，依次输入已有站点的完整名称，空行结束；随后逐段填写秒数和米数。
- 已被区间引用的站点不能直接删除。删除线路会一并删除它的全部区间。
- 保存用同一个 SQLite 事务更新三张业务表、换乘标记和受影响瓦片。失败时数据库不变；成功后刷新地图并清除旧路线。新增区间以两站间直线绘制，原有线路折线保留。
- 维护支持本项目生成器的 0–5 级瓦片。使用单个应用实例维护同一图集；其他已打开的实例需重启才能载入新数据。

旧版图集可以在关闭应用后执行 `python tools/migrate_mbtiles.py path/to/map.mbtiles` 升级；这只增加维护字段并更新版本，保留原有站线、坐标、权重和瓦片。

## 验证和预览

新增 C 测试覆盖：全部瓦片解码及区间覆盖、TMS、缓存、截断 MVT、损坏 gzip、解析器随机输入、Unicode 双列占位、三种搜索、正反向及不可达规划、同站路线、缩放锚点、六种窗口尺寸。

可导出 C 渲染器实际生成的字符帧，再离线预览：

```powershell
./cmake-build-debug/GZ_metro_planner.exe --snapshot cmake-build-debug/route.json 140 48 tyxl gznz
python tools/preview_map.py cmake-build-debug/route.json
```

PNG 是字符帧预览，不是 Windows Terminal 截图；实际字体、颜色与输入法表现取决于终端。项目已通过 ConPTY 执行搜索、自动规划和正常退出；拖动窗口及特定输入法应在目标终端补充验收。

数据来源与依赖记录见 `data/source/PROVENANCE.md` 和 `lib/README.md`。

## 列车动画

全网模式每条线路一辆列车。普通连续路径往返，环线循环；分支与断开区段轮流展示，切换时短暂隐藏。规划成功后只显示一辆行程演示列车，沿实际规划区间跨换乘点连续前进，不停站，到终点立即回起点重播；同站路线不显示列车。清除规划后恢复全网列车。

速度集中定义在 `src/render/trains.h` 的 `TRAIN_SPEED_MULTIPLIER`，默认 `3.0`，全网和行程共用。区间动画时长为数据库区间秒数除以该倍率。`TRAIN_FRAME_MS` 控制更新间隔，`TRAIN_CARRIAGE_SCALE` 控制车厢出现的缩放级别。

地图焦点下按空格暂停/继续；搜索时继续运行，维护界面和过小窗口暂停。界面不额外增加动画模式或速度提示。小比例显示方向字符，放大后显示短车厢；标签和站点优先，车厢放不下退为单字符，头部被遮挡则隐藏。每次加载图集从 0 级 MVT 缓存折线和累计长度，运行时只更新内存时间；不修改地图数据。动画不是实时运营数据。
