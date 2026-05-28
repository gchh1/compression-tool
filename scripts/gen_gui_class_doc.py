"""Generate class section for gui_过程论述.txt (one-off helper)."""
from __future__ import annotations

import re
from pathlib import Path

GUI = Path(__file__).resolve().parents[1] / "src" / "gui"
OUT = Path(__file__).resolve().parents[1] / "docs" / "_class_section_tmp.txt"

FILE_ORDER = [
    "main.py",
    "models.py",
    "config/settings.py",
    "config/theme.py",
    "engine/bridge.py",
    "engine/compressor.py",
    "engine/file_protocol.py",
    "engine/token_parser.py",
    "engine/web_dict.py",
    "engine/decompress_log.py",
    "engine/network_transfer.py",
    "engine/ffmpeg_codec.py",
    "ade/types.py",
    "ade/engine.py",
    "ade/training.py",
    "ade/explorer.py",
    "ade/explore_log.py",
    "ade/params.py",
    "ade/retrain.py",
    "ade/checkpoint.py",
    "ade/streaming_explore.py",
    "ade/ea_tune.py",
    "ade/stage2_train.py",
    "ade/nn_train.py",
    "ade/rf_train.py",
    "ui/main_window.py",
    "ui/worker.py",
    "ui/table.py",
    "ui/helpers.py",
    "ui/network_transfer_worker.py",
    "ui/panels/info_panel.py",
    "ui/dialogs/heatmap_dialog.py",
    "ui/dialogs/block_heatmap_dialog.py",
    "ui/dialogs/huffman_dialog.py",
    "ui/dialogs/lz_demo_dialog.py",
    "ui/dialogs/flate_demo_dialog.py",
    "ui/dialogs/network_sim_dialog.py",
    "ui/views/visualizers/token_heatmap.py",
    "ui/views/visualizers/huffman.py",
    "windows/comparison.py",
    "windows/heatmap.py",
    "windows/token_heatmap.py",
    "windows/webpage_heatmap.py",
    "windows/network_sim.py",
    "utils/logging.py",
    "utils/interaction_log.py",
    "utils/resources.py",
    "utils/workspace.py",
]

CLASS_BRIEF: dict[str, str] = {
    "ResourceType": "按扩展名归类资源，驱动默认算法与表格类型列。",
    "CompressionStatus": "单行压缩生命周期：pending/compressing/done/failed/skipped。",
    "AlgorithmType": "全局算法枚举，与 WCX algo_code、算法下拉框、C++ 管线 ID 一致。",
    "AlgorithmParamDef": "算法配置对话框中单参数的元数据：key、label、default、min、max、step、choices。",
    "Record": "FileRecord 与 FolderRecord 的抽象基类，定义 load_raw_data、extract_features 钩子。",
    "FileRecord": "单文件业务对象：路径、原文、压缩结果、ADE 特征与决策，存于表格 Qt.UserRole。",
    "FolderRecord": "文件夹批量对象：懒加载子 FileRecord 列表与汇总统计。",
    "NetworkProfile": "网络仿真档位：名称、带宽 bps、单向时延 ms；transfer_time 估算传输秒数。",
    "ArchiveEntry": "文件夹 WCX 归档内单条条目（名称、大小、是否目录）。",
    "Theme": "frozen dataclass，承载 bg_primary、accent、chart_1 等 QSS 颜色字段；to_dict/from_dict 序列化。",
    "ThemeManager": "单例：apply 切换 Theme，生成 main_window_sheet、table_sheet、full_dialog_sheet 等 QSS。",
    "CompressionEngine": "GUI 压缩解压门面：内存 compress、流式 smart_compress_file、WCX pack/unpack、配置锁与快照。",
    "CompressedFileHeader": "WCX v3 头：algorithm、original_size、compressed_size、flags（文件夹/网页字典）；to_bytes/from_bytes。",
    "TokenType": "枚举 LITERAL、MATCH、LITERAL_RUN。",
    "Token": "可视化单元：原文区间、压缩比特代价、match_offset、bit_offset 等；compression_ratio 属性。",
    "ParseResult": "parse 输出：tokens 列表、original_size、dp_steps、huffman_trees。",
    "HuffmanCodeEntry": "Huffman 码表项：symbol、frequency、code、code_length。",
    "HuffmanTreeNode": "Huffman 树节点：symbol、frequency、left、right；is_leaf 判断。",
    "HuffmanTreeData": "一棵树的数据：根节点、codes 列表、tree_type、total_bits。",
    "TokenParser": "解析器抽象基类，子类实现 parse(compressed_data, raw_data, compression_params)。",
    "LZSSTokenParser": "LZSS 1-bit flag + 匹配字解析。",
    "LZDPTokenParser": "LZDP：优先 C++ DP 可视化，否则逆向解析比特流。",
    "DeflateTokenParser": "Deflate/DPFlate 块级 Huffman + LZ 引用解析。",
    "_LSBBitReader": "Deflate 解析内部 LSB 位读取器。",
    "_CanonicalHuffmanTree": "由码长数组构建 canonical Huffman 并解码。",
    "DecisionMode": "ADE 模式枚举：RULE_BASED、ML_HYBRID、ML_ONLY。",
    "DecisionResult": "决策输出 dataclass：algorithm、confidence、reason、params、mode_used、is_model_decision 等。",
    "ParamRegressionSample": "Stage2a 参数回归单条样本（特征、实际/预测参数、压缩率）。",
    "TrainingSample": "早期训练样本结构（兼容保留）。",
    "DecisionEngine": "ADE 单例：decide、collect_training_sample、train、save/load_training_data。",
    "RandomForestStrategy": "继承 DecisionEngine，decide 走 C++ 随机森林模型。",
    "NeuralNetworkStrategy": "继承 DecisionEngine，decide/train 走 C++ 或内置 NN。",
    "StrategyDispatcher": "批量 dispatch/dispatch_and_compress，供 CLI 或实验脚本。",
    "TrainingSampleV3": "v3 JSONL 一行样本：特征向量、算法 ID、压缩率、探索标记等。",
    "TrainingDataStore": "单例：add_from_record、save/load JSONL、get_feature_matrix、export_csv。",
    "ArmStats": "静默探索单臂统计：mean_ratio、count、sum_ratio、min_ratio。",
    "FeatureClusterSpace": "20 维特征向量聚类到 cluster_id（最多 64 簇）。",
    "SilentExplorer": "AC-UCB 静默探索：maybe_explore、user_compression_priority、apply_tunables_from_settings。",
    "ParameterRegressor": "Stage2a C++ ParamRegressorNet 桥接：predict、train、ingest_v3_sample。",
    "ExploreDispatch": "探索任务调度元数据（cluster、UCB gap、目标算法）。",
    "CheckpointManifest": "流式探索被取消时的作业清单 JSON 序列化。",
    "ExploreStreamResult": "探索压缩结果 dataclass：success、wcx_bytes、ratio、error 等。",
    "NNTrainResult": "神经网络训练结果摘要 dataclass。",
    "NNTrainWorker": "QThread：后台 train_nn_from_jsonl，progress/done 信号。",
    "Stage2TrainResult": "Stage2 训练结果 dataclass。",
    "Stage2TrainWorker": "QThread：Stage2 参数网络训练。",
    "RFTrainResult": "随机森林训练结果 dataclass。",
    "RFTrainWorker": "QThread：train_rf_from_jsonl。",
    "NetworkBenchmarkItem": "单文件网络实测输入：wire 字节、算法、原始大小。",
    "NetworkBenchmarkTarget": "实测目标：items 列表或文件夹聚合。",
    "ProfileBenchmarkResult": "单档位 benchmark 结果：profile 名、耗时、吞吐、解压耗时。",
    "VideoFFmpegCompressor": "FFmpeg H.264/H.265 内存压缩解压，compress/decompress 返回 dict。",
    "StatusBarWidget": "主窗口底部状态文案与进度条。",
    "AlgorithmSelector": "工具栏算法 QComboBox，current_algorithm 返回 AlgorithmType。",
    "ThemeConfigDialog": "主题色编辑、亮暗预设、实时预览。",
    "DecisionEngineManagerDialog": "ADE 引擎/探索/训练/配置四页签管理对话框。",
    "MinMatchWidget": "min_match 旋钮与「自动」勾选组合控件。",
    "AlgorithmConfigDialog": "各算法 SpinBox/Combo 动态表单，应用后 CompressionEngine.set_config。",
    "CompressDemoDialog": "压缩演示入口壳（按记录打开子演示）。",
    "MainWindow": "QMainWindow：拖放、压缩解压导出、可视化槽、主题刷新。",
    "FolderReportWidget": "文件夹压缩汇总报告面板。",
    "DecisionDetailDialog": "展示 FileRecord.decision_result 字段。",
    "CompressionWorker": "压缩后台线程：single_compress、流式/内存分支、训练样本写入。",
    "ComparisonWorker": "多算法对比线程：_run_file_comparison、comparison_finished 信号。",
    "FileTableWidget": "七列表格：add_file、update_row、右键信号、UserRole 存 Record。",
    "NetworkTransferWorker": "网络实测 QThread：benchmark_all_profiles。",
    "InfoPanel": "LZDP 演示侧栏信息区。",
    "HeatmapDialog": "内嵌 Token 热力图三件套（文本/信息/比特流）的对话框。",
    "BlockHeatmapCanvas": "按块压缩率矩阵着色画布。",
    "BlockHeatmapDialog": "块热力图对话框壳。",
    "HuffmanTreeDialog": "嵌入 HuffmanTreePanel 的树与码表对话框。",
    "DPArrayBar": "LZDP DP 数组柱状条可视化。",
    "LZDPDPSliderWidget": "LZDP 逐步 DP 滑块演示主控件。",
    "LZDPDPDialog": "LZDPDPSliderWidget 的 QDialog 包装。",
    "LZSliderWidget": "LZSS/LZ 贪心逐步滑块演示。",
    "LZSliderDialog": "LZSliderWidget 的 QDialog 包装。",
    "_BuildStep": "Huffman 建树动画单步快照。",
    "_QueueNode": "建树动画优先队列节点模型。",
    "_ZoomableTreeCanvas": "可缩放平移的画布，承载自定义 paint_fn。",
    "HuffmanBuildAnimator": "Deflate Huffman 建树逐步回放控件。",
    "FlateDemoDialog": "Deflate 演示：LZ 页 + Huffman 建树页。",
    "NetworkSimDialog": "网络仿真：公式卡片 + 实测进度 + HTML 导出。",
    "TokenHeatmapWidget": "QPlainTextEdit 上对 Token 区间 ExtraSelection 高亮。",
    "TokenInfoPanel": "悬停 Token 时显示字段文本。",
    "BitstreamWidget": "逐比特小格热力图。",
    "HuffmanTreeWidget": "双树（literal/length + distance）切换绘制。",
    "HuffmanTreePanel": "树画布 + 频谱图组合面板。",
    "HuffmanTreeCanvas": "递归布局 HuffmanTreeNode 并 QPainter 绘制。",
    "HuffmanFreqChart": "符号频率柱状图。",
    "ComparisonDialog": "内嵌 HTML 或 WebEngine 的算法对比结果对话框。",
    "NetworkSimInput": "网络报告生成输入：总字节、总压缩字节、名称。",
    "ResourceReference": "网页热力图中单条资源引用（URL、类型、压缩率）。",
}


def parse_file(path: Path) -> tuple[dict, list[str]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    classes: dict[str, dict] = {}
    module_funcs: list[str] = []
    current: str | None = None
    for i, line in enumerate(lines):
        m = re.match(r"^class (\w+)", line)
        if m:
            current = m.group(1)
            doc = ""
            j = i + 1
            while j < len(lines) and lines[j].strip().startswith("@"):
                j += 1
            if j < len(lines) and lines[j].strip().startswith('"""'):
                if lines[j].count('"""') >= 2:
                    doc = lines[j].strip().strip('"').strip()[:200]
                else:
                    j += 1
                    parts = []
                    while j < len(lines) and '"""' not in lines[j]:
                        parts.append(lines[j].strip())
                        j += 1
                    doc = " ".join(parts)[:200]
            classes[current] = {"methods": [], "signals": [], "doc": doc, "line": i + 1}
            continue
        if current:
            ms = re.match(r"^    def (\w+)\(", line)
            if ms:
                classes[current]["methods"].append(ms.group(1))
            sig = re.match(r"^    (\w+) = pyqtSignal", line)
            if sig:
                classes[current]["signals"].append(sig.group(1))
        mf = re.match(r"^def (\w+)\(", line)
        if mf and not mf.group(1).startswith("_"):
            module_funcs.append(mf.group(1))
    return classes, module_funcs


def format_class_block(rel: str, cname: str, info: dict) -> list[str]:
    brief = CLASS_BRIEF.get(cname) or info["doc"] or "见上文实现路径及源码。"
    out = [cname]
    out.append(f"  所在：src/gui/{rel} 约第 {info['line']} 行。")
    out.append(f"  说明：{brief}")
    if info["signals"]:
        out.append(f"  Qt 信号：{', '.join(info['signals'])}。")
    pub = [
        m
        for m in info["methods"]
        if not (m.startswith("_") and m not in ("__init__", "__post_init__"))
    ]
    if pub:
        out.append(f"  主要方法/接口：{', '.join(pub)}。")
    priv = [
        m
        for m in info["methods"]
        if m.startswith("_") and m not in ("__init__", "__post_init__")
    ]
    if priv:
        shown = priv[:15]
        suffix = "…" if len(priv) > 15 else ""
        out.append(f"  辅助/内部方法：{', '.join(shown)}{suffix}。")
    out.append("")
    return out


def main() -> None:
    lines_out: list[str] = [
        "",
        "二十三、各类定义与接口说明",
        "",
        "本节对 GUI 源码中每一个 class（含 Enum、dataclass、QWidget、QThread）给出：所在文件、职责说明、对外方法或 Qt 信号、必要的内部辅助方法名。各小节末尾列出该文件模块级辅助函数（不含以下划线开头的私有函数）。",
        "",
    ]
    sec = 0
    seen_files: set[str] = set()

    for rel in FILE_ORDER:
        path = GUI / rel
        if not path.exists():
            continue
        seen_files.add(rel)
        sec += 1
        classes, mod_funcs = parse_file(path)
        lines_out.append(f"23.{sec} src/gui/{rel}")
        lines_out.append("")
        for cname, info in classes.items():
            lines_out.extend(format_class_block(rel, cname, info))
        if mod_funcs:
            lines_out.append(
                f"  模块级辅助函数：{', '.join(mod_funcs)}。"
            )
            lines_out.append("")

    for p in sorted(GUI.rglob("*.py")):
        rel = p.relative_to(GUI).as_posix()
        if rel in seen_files or rel.endswith("__init__.py"):
            continue
        classes, mod_funcs = parse_file(p)
        if not classes and not mod_funcs:
            continue
        sec += 1
        lines_out.append(f"23.{sec} src/gui/{rel}")
        lines_out.append("")
        for cname, info in classes.items():
            lines_out.extend(format_class_block(rel, cname, info))
        if mod_funcs:
            lines_out.append(f"  模块级辅助函数：{', '.join(mod_funcs)}。")
            lines_out.append("")

    OUT.write_text("\n".join(lines_out), encoding="utf-8")
    print(f"Wrote {len(lines_out)} lines -> {OUT}")


if __name__ == "__main__":
    main()
