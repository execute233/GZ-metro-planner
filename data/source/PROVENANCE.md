# 素材与数据制作记录

2026-09-09 更新：运行时图集的几何已改为从 `../calibration/network-labeled.svg` 导入，
不再使用下述旧颜色追踪结果。新增离线导入器按逻辑站点合并多线路锚点、平滑连接区间，
保留 SQLite 中的站名、搜索索引和权重，原图集在 `../../output/svg-import-backups/` 备份。
`network.json`、OCR 及以下记录保留用于追溯初始站序来源。

记录日期：2026-09-08。

- 几何素材：用户提供的 `../railway.png`，4096×4096；不推定其版权或运营版本。
- 站序辅助来源（本地快照）：
  - https://map.amap.com/service/subway?_1469083453978&srhdata=4401_drw_guangzhou.json
  - https://map.amap.com/service/subway?_1469083453978&srhdata=4406_drw_foshan.json
- 快照用于核对站名和相邻关系；几何位置来自用户图片及 OCR/颜色追踪，不使用高德的示意坐标代替用户图片。
- OCR：RapidOCR ONNXRuntime 1.4.4，按重叠区域识别，结果保存在 `ocr.json`。拼音由 pypinyin 0.55.0 生成，允许在源 JSON 中人工修正多音字。
- `station_overrides.json` 修正 OCR 无法完整读取的 10 个站名位置。局部配准使用原图像素坐标。
- 原图中的佛山 3 号线在中山公园与联和之间留有断口，不添加外部快照中的敦厚、佛山站和三段连接。`skipped_edges` 记录这三段明确排除的连接；其余追踪失败会阻止发布。
- 原图未展开 APM 和有轨电车，排除这些辅助数据源中的线路。
- 交通权重原为占位的每区间 1000m、60s，2026-09-15 已由 `tools/apply_edge_weights.py`
  按 `../calibration/` 之外的《粤港澳大湾区地铁城际配线图 v2.0.7》逐区间读数替换：
  里程取自图上标注的站间距（米），时长按用户提供的分线路平均旅行速度换算
  （`seconds = meters × 3.6 / v_kmh`）。逐区间读数与人工复核的权重表按本地临时文件保存，
  不随仓库分发（遵循 AGENTS.md 不提交临时目录的规定）；重跑导入器需自备该表，
  表头为 `edge_id line from to meters seconds speed_kmh source`，每行标注读数来源。
  全网合计 852.8 km。
  注意图上远郊段是压缩绘制的（14 号线从化段约 50–70 m/pt，市区约 14 m/pt），
  里程只能取自图上标注的数字，不能用坐标反推。
- 当前为待校对初版；自动识别匹配成功不等于通过逐站人工核验。外部数据来源与图片版本可能存在差异，不将其标为实时/官方运营数据。

日常维护入口：地图中按 M，直接写入 SQLite。`network.json` 仅为离线导入素材，用 `tools/build_mbtiles.py` 重新导入会覆盖数据库内维护结果，需先备份。原始快照保留用于追溯，不作为应用的网络依赖。
