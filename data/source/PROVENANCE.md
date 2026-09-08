# 素材与数据制作记录

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
- 交通权重按用户要求全部暂定为每区间 1000m、60s。
- 当前为待校对初版；自动识别匹配成功不等于通过逐站人工核验。外部数据来源与图片版本可能存在差异，不将其标为实时/官方运营数据。

维护入口：编辑 `network.json` 后用 `tools/build_mbtiles.py` 重新生成。原始快照保留用于追溯，不作为应用的网络依赖。
