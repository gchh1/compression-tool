# å·¥ä½œäº¤æŽ¥æŠ¥å‘Š

> **ç”Ÿæˆæ—¥æœŸ**: 2026-05-10  
> **é¡¹ç›®**: WebCompress åŽ‹ç¼©å·¥å…·  
> **å½“å‰é˜¶æ®µ**: LZDP/DPFlate ç®—æ³•é‡æž„ + ADE æ¨¡å—å¾…é›†æˆ

---

## 1. å½“å‰é¡¹ç›®çŠ¶æ€æ€»è§ˆ

### 1.1 å·²å®Œæˆçš„å·¥ä½œ

| æ¨¡å— | çŠ¶æ€ | è¯´æ˜Ž |
|------|------|------|
| **LZDP ç®—æ³•** | âœ… åŸºæœ¬å®Œæˆ | KMP + DP ä¼˜åŒ– + çŽ¯å½¢é˜Ÿåˆ— top-n + offset=0 literal run ç¼–ç  |
| **DPFlate ç®—æ³•** | ðŸ”„ éƒ¨åˆ†å®Œæˆ | åŽ‹ç¼©ç«¯: LZDP + Huffman å·²å®žçŽ°; è§£åŽ‹ç«¯: DPFlateDecompress éª¨æž¶å·²å†™ä½†æœªéªŒè¯ |
| **ADE æ¨¡å— (C++)** | âœ… å·²å®Œæˆ | RandomForest, DecisionEngine, FeatureExtractorV3, EA ç®—æ³•, pybind11 ç»‘å®š |
| **ADE æ•°æ®ç®¡çº¿ (Python)** | âœ… å·²å®Œæˆ | feature_extractor.py, training_store.py, explorer.py, decision.py |
| **ADE è®¾è®¡æ–‡æ¡£** | âœ… å·²å®Œæˆ | docs/design/algorithm_decision_engine_design.md (1870 è¡Œ) |
| **LZ ç¼–ç è®¨è®ºæ–‡æ¡£** | âœ… å·²å®Œæˆ | docs/design/lz_token_encoding_discussion.md |
| **DP è®¾è®¡æ–‡æ¡£** | âœ… å·²å®Œæˆ | docs/design/lzdp_dp_design.md |
| **ç‰¹å¾å‘é‡è§„æ ¼æ–‡æ¡£** | âœ… å·²å®Œæˆ | docs/specs/feature_vector_specification_v3_final.md |
| **ADE å·¥ä½œäº¤æŽ¥ Skill** | âœ… å·²å®Œæˆ | .trae/skills/ade-handover/SKILL.md |

### 1.2 å­˜åœ¨çš„é—®é¢˜

| é—®é¢˜ | ä¸¥é‡æ€§ | è¯´æ˜Ž |
|------|--------|------|
| **LZDP è§£åŽ‹ "Offset out of range"** | ðŸ”´ P0 | è§ä¸‹æ–¹è¯¦ç»†åˆ†æž |
| **DPFlateCompressor::decompress ç”¨äº† Inflate** | ðŸ”´ P0 | DPFlateCompressor.cpp#L67 ä½¿ç”¨ `algorithm::Inflate` è€Œéž `DPFlateDecompress` |
| **DPFlateDecompress æœªéªŒè¯** | ðŸ”´ P0 | DPFlate.cpp ä¸­çš„è§£åŽ‹å®žçŽ°æœªç»è¿‡ roundtrip æµ‹è¯• |
| **Literal run é•¿åº¦æº¢å‡º** | ðŸ”´ P0 | `compress_ultra` ä¸­ literal run é•¿åº¦å¯èƒ½è¶…è¿‡ `length_bits_` å¯è¡¨ç¤ºçš„æœ€å¤§å€¼ |
| **åŽ‹ç¼©æ¼”ç¤ºæ‰“ä¸å¼€** | ðŸ”´ P0 | LZDP è§£åŽ‹æŠ¥é”™å¯¼è‡´æ¼”ç¤ºé¡µé¢æ— æ³•æ¸²æŸ“ |
| **ADE C++ æ¨¡å—æœªé›†æˆåˆ°æž„å»º** | ðŸŸ¡ P1 | CMake æž„å»º + pybind æ¨¡å—æœªåœ¨ç”¨æˆ·æœºå™¨éªŒè¯ |
| **ADE æ¨¡åž‹æœªå®žé™…æŠ•å…¥ä½¿ç”¨** | ðŸŸ¡ P1 | AUTO æ¨¡å¼ä»ä½¿ç”¨ heuristic è€Œéž ML å†³ç­– |

---

## 2. LZDP è§£åŽ‹ Bug è¯¦ç»†åˆ†æž

### 2.1 é”™è¯¯ä¿¡æ¯

```
Offset in LZDP decompression out of range
```

### 2.2 è§¦å‘ä½ç½®

`d:\AAA_C\compression-tool\src\algorithm\LZDP.cpp#L410`

```cpp
if (out_size < static_cast<size_t>(offset)) {
    throw std::runtime_error("Offset in LZDP decompression out of range");
}
```

### 2.3 æ ¹å› åˆ†æž

**æ ¹å›  1: Literal run é•¿åº¦æº¢å‡º `length_bits_`**

åœ¨ `compress_ultra` å‡½æ•°ä¸­ (LZDP.cpp#L215-L225):
```cpp
bw.writeBits(run_len, static_cast<int>(length_bits_));
```

`run_len` æ˜¯è¿žç»­ literal token çš„æ•°é‡ï¼Œå¯èƒ½éžå¸¸å¤§ï¼ˆä¾‹å¦‚æ•´ä¸ªæ–‡ä»¶æ— åŒ¹é…æ—¶ç­‰äºŽæ–‡ä»¶å¤§å°ï¼‰ã€‚  
å¦‚æžœ `run_len > (1 << length_bits_) - 1`ï¼Œå†™å…¥æ—¶ä¼šè¢«æˆªæ–­ï¼Œè§£åŽ‹æ—¶è¯»åˆ°é”™è¯¯çš„å€¼ï¼Œå¯¼è‡´åŽç»­æ‰€æœ‰ token ä½å¯¹é½é”™ä¹±ã€‚

**ç¤ºä¾‹**: `lookahead_size = 258` â†’ `length_bits_ = 9` â†’ max = 511  
ä½†å¦‚æžœ `search_size = 4096` â†’ `offset_bits_ = 13`  
å¯¹äºŽ 1000 å­—èŠ‚æ— åŒ¹é…çš„æ–‡ä»¶ï¼Œ`run_len = 1000` > 511 â†’ æˆªæ–­ â†’ è§£åŽ‹é”™ä½ â†’ è¯»åˆ°é”™è¯¯çš„ offset â†’ "out of range"

**æ ¹å›  2: DPFlateCompressor::decompress ä½¿ç”¨ Inflate**

`d:\AAA_C\compression-tool\src\core\DPFlateCompressor.cpp#L67`:
```cpp
algorithm::Inflate inflate;
inflate.reset();
auto status = inflate.process(compressed_data, out, true);
```

DPFlate çš„æ–°æ ¼å¼ (LZDP tokens + Huffman) å®Œå…¨ä¸æ˜¯ DEFLATE/Inflate æ ¼å¼ï¼Œç”¨ Inflate è§£åŽ‹å¿…ç„¶å¤±è´¥ã€‚

### 2.4 ä¿®å¤æ–¹å‘

1. **Literal run åˆ†æ®µ**: å½“ `run_len > max_length` æ—¶ï¼Œæ‹†åˆ†ä¸ºå¤šä¸ª literal run token
2. **DPFlateCompressor::decompress** æ”¹ç”¨ `algorithm::DPFlateDecompress`
3. **DPFlateDecompress å®Œæ•´å®žçŽ°**: å½“å‰éª¨æž¶å·²å†™ (DPFlate.cpp#L210-L380)ï¼Œä½† `readHuffmanTree()` å®žçŽ°ä¸å®Œæ•´ï¼ˆæœªæ­£ç¡®æž„å»º Huffman æ ‘ï¼‰ï¼Œéœ€è¦ä¿®å¤

---

## 3. åŽç»­å·¥ä½œæ–¹å‘

### 3.1 ðŸ”´ P0: LZDP/DPFlate è§£åŽ‹ä¿®å¤

| æ­¥éª¤ | è¯´æ˜Ž | æ¶‰åŠæ–‡ä»¶ |
|------|------|----------|
| 1 | ä¿®å¤ `compress_ultra` literal run åˆ†æ®µ | LZDP.cpp |
| 2 | ä¿®å¤ `compress` (greedy) literal run åˆ†æ®µ | LZDP.cpp |
| 3 | ä¿®å¤ DPFlateDecompress::readHuffmanTree | DPFlate.cpp |
| 4 | DPFlateCompressor::decompress æ”¹ç”¨ DPFlateDecompress | DPFlateCompressor.cpp |
| 5 | roundtrip æµ‹è¯•éªŒè¯ | tests/test_roundtrip.cpp |

### 3.2 ðŸŸ¡ P1: ADE æ¨¡å—é›†æˆ

| æ­¥éª¤ | è¯´æ˜Ž | æ¶‰åŠæ–‡ä»¶ |
|------|------|----------|
| 1 | CMake æž„å»º ADE + pybind æ¨¡å— | CMakeLists.txt, pybind_module.cpp |
| 2 | éªŒè¯ ADE C++ æ¨¡å—åœ¨ç”¨æˆ·æœºå™¨ç¼–è¯‘é€šè¿‡ | src/ade/ |
| 3 | è¿è¡Œ ADE å•å…ƒæµ‹è¯• | tests/cpp/ade/ |
| 4 | éªŒè¯ Python ç«¯ ADE ç»‘å®š | src/gui/ade/engine.py |
| 5 | AUTO æ¨¡å¼åˆ‡æ¢åˆ° ML å†³ç­– | src/gui/ade/engine.py |

### 3.3 ðŸŸ¡ P1: ç¼–ç æ–¹æ¡ˆä¼˜åŒ–

| æ­¥éª¤ | è¯´æ˜Ž | æ¶‰åŠæ–‡ä»¶ |
|------|------|----------|
| 1 | å®žçŽ°åŠ¨æ€ `min_match = sizeof(token)/8 + 1` | LZDP.hpp, LZDP.cpp |
| 2 | æ›´æ–° token ç¼–ç : offset=0 è¡¨ç¤º literal run | LZDP.cpp (compress/decompress) |
| 3 | æ›´æ–° lz_token_encoding_discussion.md | docs/design/lz_token_encoding_discussion.md |
| 4 | ç¼–è¯‘ + æµ‹è¯•éªŒè¯ | tests/ |

### 3.4 ðŸŸ¢ P2: åŠŸèƒ½å®Œå–„

| æ­¥éª¤ | è¯´æ˜Ž |
|------|------|
| 1 | ä¿®å¤å³é”®èœå•"ä¸¤ä¸ªç§»é™¤"é—®é¢˜ |
| 2 | å®žçŽ°åŽ‹ç¼©ç»“æžœå¯¼å‡ºåŠŸèƒ½ |
| 3 | æ‰“åŒ…æ–‡ä»¶æž¶æž„è§„èŒƒåŒ– |
| 4 | å»ºç«‹éªŒè¯è§„èŒƒæ–‡æ¡£ (docs/verification_spec.md) |
| 5 | GPU åŠ é€Ÿ (CUDA/OpenCL) |
| 6 | i18n ä¸­è‹±æ–‡åˆ‡æ¢ |

---

## 4. ADE æ¨¡å—è¯¦ç»†çŠ¶æ€

### 4.1 C++ å®žçŽ° (src/ade/) â€” âœ… å·²å®Œæˆ

| ç»„ä»¶ | æ–‡ä»¶ | è¯´æ˜Ž |
|------|------|------|
| RandomForest | src/ade/include/RandomForest.hpp | ~700 è¡Œ, Gini impurity, bootstrap, äºŒè¿›åˆ¶åºåˆ—åŒ– |
| DecisionEngine | src/ade/include/DecisionEngine.hpp | RULE_BASED / ML_HYBRID / ML_ONLY ä¸‰ç§æ¨¡å¼ |
| ADEBridge | src/ade/include/ADEBridge.hpp | C++ â†” Python æ¡¥æŽ¥ |
| FeatureExtractorV3 | src/ade/include/FeatureExtractorV3.hpp | 33 ç»´ç‰¹å¾æå– |
| EvolutionaryAlgorithms | src/ade/include/EvolutionaryAlgorithms.hpp | GA, PSO, CMA-ES |
| è®­ç»ƒæ•°æ® | assets/ade/training_data_v3.json | 3000 æ¡åˆæˆæ ·æœ¬, 33 ç»´, 8 ç±» |
| é¢„è®­ç»ƒæ¨¡åž‹ | assets/ade/default_model.bin | 50 æ£µæ ‘, ~30KB |
| è®­ç»ƒå·¥å…· | src/ade/tools/train_model.cpp | CLI é‡è®­ç»ƒ |
| æ•°æ®é›†ç”Ÿæˆ | src/ade/tools/generate_dataset.cpp | åˆæˆæ•°æ®ç”Ÿæˆ |

### 4.2 Python é›†æˆ (src/gui/ade/ 等) â€” âœ… å·²å®Œæˆ

| ç»„ä»¶ | æ–‡ä»¶ | è¯´æ˜Ž |
|------|------|------|
| ç‰¹å¾æå– | feature_extractor.py | 20 ç»´ BaseFeatures, å•æ¬¡éåŽ† O(n) |
| è®­ç»ƒå­˜å‚¨ | training_store.py | JSONL æ ¼å¼, å¢žé‡ä¿å­˜ |
| é™é»˜æŽ¢ç´¢ | explorer.py | AC-UCB ç®—æ³•, å¼‚æ­¥æ‰§è¡Œ |
| å†³ç­–å¼•æ“Ž | decision.py | Rule-based + heuristic, ADE åˆå§‹åŒ– |
| ç®¡ç† UI | main_window.py | DecisionEngineManagerDialog, 4 æ ‡ç­¾é¡µ |
| AUTO é›†æˆ | main_window.py | Heuristic å‚æ•°ç”Ÿæˆ |

### 4.3 å¾…éªŒè¯é¡¹

1. **CMake æž„å»º**: `src/ade/CMakeLists.txt` éœ€è¦åœ¨ç”¨æˆ·æœºå™¨ä¸ŠéªŒè¯
2. **pybind æ¨¡å—**: `src/bindings/pybind/pybind_module.cpp` éœ€è¦ç¼–è¯‘éªŒè¯
3. **ADE åˆå§‹åŒ–**: `decision.py::_init_ade()` éœ€è¦éªŒè¯ C++ æ¨¡å—åŠ è½½
4. **æ¨¡åž‹åŠ è½½**: `default_model.bin` è·¯å¾„éœ€è¦åœ¨ç”¨æˆ·æœºå™¨ä¸Šç¡®è®¤

---

## 5. å…³é”®æ–‡ä»¶ç´¢å¼•

| æ–‡ä»¶ | è¯´æ˜Ž |
|------|------|
| `src/algorithm/LZDP.cpp` | LZDP åŽ‹ç¼©/è§£åŽ‹/DP å¯è§†åŒ– (563 è¡Œ) |
| `src/algorithm/include/LZDP.hpp` | LZDP ç±»å®šä¹‰ (108 è¡Œ) |
| `src/algorithm/DPFlate.cpp` | DPFlate åŽ‹ç¼© + DPFlateDecompress è§£åŽ‹ (380 è¡Œ) |
| `src/algorithm/include/DPFlate.hpp` | DPFlate ç±»å®šä¹‰ (85 è¡Œ) |
| `src/algorithm/include/KMPMatcher.hpp` | KMP åŒ¹é… + çŽ¯å½¢é˜Ÿåˆ— (146 è¡Œ) |
| `src/algorithm/HuffmanTree.cpp` | Huffman æ ‘ + åºåˆ—åŒ–/ååºåˆ—åŒ– |
| `src/algorithm/include/HuffmanTree.hpp` | HuffmanTree ç±»å®šä¹‰ |
| `src/core/DPFlateCompressor.cpp` | DPFlateCompressor (87 è¡Œ, è§£åŽ‹éœ€ä¿®å¤) |
| `src/core/LZDPCompressor.cpp` | LZDPCompressor (69 è¡Œ) |
| `src/core/AlgorithmFactory.cpp` | ç®—æ³•å·¥åŽ‚ (108 è¡Œ) |
| `src/ade/include/RandomForest.hpp` | RandomForest C++ å®žçŽ° (~700 è¡Œ) |
| `src/ade/include/DecisionEngine.hpp` | ADE å†³ç­–å¼•æ“Ž |
| `src/ade/include/ADEBridge.hpp` | C++ â†” Python æ¡¥æŽ¥ |
| `src/bindings/pybind/pybind_module.cpp` | pybind11 ç»‘å®š (245 è¡Œ) |
| `src/gui/ade/engine.py` | Python å†³ç­–å¼•æ“Ž (åŽŸ core/decision) |
| `src/gui/ade/explorer.py` | é™é»˜æŽ¢ç´¢ç­–ç•¥ |
| `src/gui/ade/features.py` | ç‰¹å¾æå– |
| `src/gui/ade/training.py` | è®­ç»ƒæ•°æ®å­˜å‚¨ |
| `src/gui/ui/main_window.py` | ä¸»çª—å£ + åŽ‹ç¼©æ¼”ç¤º (原 widgets/main_window) |
| `docs/design/algorithm_decision_engine_design.md` | ADE è®¾è®¡æ–‡æ¡£ (1870 è¡Œ) |
| `docs/design/lz_token_encoding_discussion.md` | LZ ç¼–ç è®¨è®º |
| `docs/design/lzdp_dp_design.md` | DP ç®—æ³•è®¾è®¡ |
| `docs/specs/feature_vector_specification_v3_final.md` | ç‰¹å¾å‘é‡è§„æ ¼ |
| `.trae/skills/ade-handover/SKILL.md` | ADE å·¥ä½œäº¤æŽ¥ Skill |

---

## 6. å·²çŸ¥ Bug æ¸…å•

### Bug 1: LZDP è§£åŽ‹ "Offset out of range" ðŸ”´
- **ä½ç½®**: LZDP.cpp#L410
- **æ ¹å› **: Literal run é•¿åº¦å¯èƒ½è¶…è¿‡ `length_bits_` æœ€å¤§å€¼ï¼Œå¯¼è‡´ä½å¯¹é½é”™ä¹±
- **å¤çŽ°**: åŽ‹ç¼©ä»»ä½•æ–‡ä»¶åŽç”¨ LZDP è§£åŽ‹
- **ä¿®å¤**: `compress_ultra` å’Œ `compress` ä¸­ literal run éœ€è¦åˆ†æ®µå†™å…¥

### Bug 2: DPFlateCompressor::decompress ç”¨ Inflate ðŸ”´
- **ä½ç½®**: DPFlateCompressor.cpp#L67
- **æ ¹å› **: æœªæ›´æ–°ä¸º DPFlateDecompress
- **ä¿®å¤**: æ›¿æ¢ä¸º `algorithm::DPFlateDecompress`

### Bug 3: DPFlateDecompress::readHuffmanTree ä¸å®Œæ•´ ðŸ”´
- **ä½ç½®**: DPFlate.cpp#L228-L260
- **æ ¹å› **: è¯»å– Huffman æ ‘åŽæœªæ­£ç¡®æž„å»º `HuffmanTree` å¯¹è±¡
- **ä¿®å¤**: ä½¿ç”¨ `HuffmanTree(BitReader&, ...)` æž„é€ å‡½æ•°

### Bug 4: åŽ‹ç¼©æ¼”ç¤ºæ— æ³•æ‰“å¼€ ðŸ”´
- **ä½ç½®**: main_window.py#L1791-L1851
- **æ ¹å› **: LZDP è§£åŽ‹å¤±è´¥ + DPFlate è§£åŽ‹ç”¨ Inflate
- **ä¿®å¤**: å…ˆä¿®å¤ Bug 1-3

---

## 7. å»ºè®®çš„ä¸‹ä¸€æ­¥å·¥ä½œé¡ºåº

```
Week 1: ðŸ”´ P0 Bug ä¿®å¤
  Day 1-2:  ä¿®å¤ LZDP literal run åˆ†æ®µ (Bug 1)
  Day 3-4:  ä¿®å¤ DPFlateDecompress (Bug 3)
  Day 5:    ä¿®å¤ DPFlateCompressor::decompress (Bug 2)
  Day 6-7:   roundtrip æµ‹è¯• + åŽ‹ç¼©æ¼”ç¤ºéªŒè¯

Week 2: ðŸŸ¡ P1 ADE é›†æˆ
  Day 1-2:   CMake æž„å»º ADE + pybind æ¨¡å—
  Day 3-4:   éªŒè¯ ADE C++ æ¨¡å—ç¼–è¯‘ + æµ‹è¯•
  Day 5:     éªŒè¯ Python ç«¯ ADE ç»‘å®š
  Day 6-7:   AUTO æ¨¡å¼åˆ‡æ¢åˆ° ML å†³ç­–

Week 3: ðŸŸ¡ P1 ç¼–ç æ–¹æ¡ˆä¼˜åŒ–
  Day 1-3:   å®žçŽ°åŠ¨æ€ min_match = sizeof(token)/8 + 1
  Day 4-5:   æ›´æ–° token ç¼–ç  (offset=0 literal run)
  Day 6-7:   ç¼–è¯‘ + æµ‹è¯• + æ–‡æ¡£æ›´æ–°

Week 4: ðŸŸ¢ P2 åŠŸèƒ½å®Œå–„
  - å³é”®èœå•ä¿®å¤
  - åŽ‹ç¼©ç»“æžœå¯¼å‡º
  - æ‰“åŒ…æž¶æž„è§„èŒƒåŒ–
  - éªŒè¯è§„èŒƒæ–‡æ¡£
```

---

*æœ¬æŠ¥å‘Šç”± AI Assistant æ ¹æ®å½“å‰ä»£ç åº“çŠ¶æ€ç”Ÿæˆ*  
*æœ€åŽæ›´æ–°: 2026-05-10*
