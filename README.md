# 广州地铁 · 终端地图规划

C23 / Windows Terminal / PDCursesMod。地图由 SQLite 查询本地 MBTiles，解压 gzip 后解析 MVT v2，使用 Braille 绘制线路，中文站名单独显示。运行时不需要 Python、Go 或网络。

## 构建与运行

需要 CMake 4.0+、MinGW GCC、Ninja。SQLite 3.50.4 和 zlib 1.3.1 的 C 源码已随项目提供；首次配置会获取固定版本 PDCursesMod。

```powershell
cmake -S . -B cmake-build-debug -G Ninja
cmake --build cmake-build-debug
ctest --test-dir cmake-build-debug --output-on-failure
./cmake-build-debug/GZ_metro_planner.exe data
```

推荐深色 Windows Terminal、支持中文的等宽字体、120×40 或更大窗口。背景与普通文字使用终端默认色，线路对深色背景做亮度调整。

如果已准备 PDCursesMod 源码，可以离线配置：

```powershell
cmake -S . -B cmake-build-debug -G Ninja -DGZMP_PDCURSES_SOURCE_DIR='E:/deps/PDCursesMod'
```

固定提交：`520adbae06981c8eb9c0222bc582b6435329335e`。

也可以传入另一份由本项目工具生成的图集：

```powershell
./cmake-build-debug/GZ_metro_planner.exe data/metro.mbtiles
```

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
| 维护站点和线路 | 地图中 M，Esc 取消并返回 |
| 退出 | 地图中 Q；任意位置 Ctrl+C |

确认起终点后自动规划，改变目标后自动重算。输入文字时不触发地图快捷键。总览只显示轮廓；放大后先显示换乘站标签，再显示普通站名，重叠标签会避让。起终点使用 `@` 标记。

90 列以上地图与右栏并排；较窄窗口按焦点切换地图与表单页。最低 50×16。

## 当前图集及数据边界

- 以用户提供的 `data/railway.png`（4096×4096）为几何参考，离线匹配站名并沿线路颜色追踪折线。
- 图集初版含 **357 个站点、21 个线路标识、421 个区间**。3 号线分支共用线路标识，14 号线支线独立标识；12 号线和佛山 3 号线保留图中未连接的区段。
- APM、海珠有轨电车和南海有轨电车未在这张图片中展开，当前图集不额外添加它们。
- **每个区间暂定 1000 米、60 秒**。这些不是实际运行数据，暂不包含候车或换乘步行时间。
- 几何为自动提取后作局部修正的初版，保留待校对标记；不能视为经过逐站人工验收的运营线网。特别应检查换乘位置、曲线走向与图片/外部站序的版本差异。
- 示意坐标映射到虚拟墨卡托范围，仅用于瓦片定位，不能用它计算实际距离。

## 校对与重新生成

`data/source/network.json` 是离线导入素材，应用运行和维护不读取它：站点、拼音、坐标、线路色、区间端点、折线和权重都在其中。日常增删请使用地图内维护。直接修改 SQLite 的 `edges.seconds` / `edges.meters` 后重启可更新规划权重；不要修改瓦片二进制来调整权重。

重新导入会覆盖 SQLite 中的维护结果，请先备份数据库。仅离线制图需要 Python。建议使用独立虚拟环境：

```powershell
python -m venv .venv-map
./.venv-map/Scripts/python.exe -m pip install --only-binary=:all: -r tools/requirements.txt
./.venv-map/Scripts/python.exe tools/build_mbtiles.py
```

生成器使用 MVT v2、4096 extent、0～5 级缩放、gzip 压缩和 MBTiles TMS 行号；先生成临时数据库，通过完整性检查后替换目标。源数据变化后应关闭应用、重新生成并重启，确保缓存和规划数据一致。

完整的重新提取流程为 `tools/ocr_map.py` → `tools/prepare_network.py --force` → `tools/build_mbtiles.py`。**重新提取会重建源数据并覆盖人工改动**；仅在有意重新导入整个图集时修改 `network.json` 并运行最后一步。`station_overrides.json` 保存局部位置修正。

MBTiles 增加 `stations`、`lines`、`edges` 表存放规划与搜索数据；标准地图软件可以读取其中的标准 `metadata` / `tiles`，本程序则要求 `gzmp_schema=2` 的应用数据。它不是通用的任意 MBTiles 查看器。

## 地图内维护

`data/metro.mbtiles`（SQLite）是应用唯一的数据源。旧独立文字模式及 CSV 数据文件已移除。

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
./cmake-build-debug/GZ_metro_planner.exe --snapshot data/metro.mbtiles cmake-build-debug/route.json 140 48 tyxl gznz
python tools/preview_map.py cmake-build-debug/route.json
```

PNG 是字符帧预览，不是 Windows Terminal 截图；实际字体、颜色与输入法表现取决于终端。项目已通过 ConPTY 执行搜索、自动规划和正常退出；拖动窗口及特定输入法应在目标终端补充验收。

数据来源与依赖记录见 `data/source/PROVENANCE.md` 和 `third_party/README.md`。
