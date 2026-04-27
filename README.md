# compression-tool

## project layout
```
. /
├── build/
├── docs/                     # 任务书、设计报告等
├── tests/                    # 测试  
├── third_party/              # 放 pybind11 等第三方库源码
├── src/
│   ├── bindings/             # pybind / wasm 胶水代码
│   ├── algorithm/            # lz77, humman 等基础算法
│   ├── core/                 # 压缩算法
│   │   ├── include/      
│   │   └── src/          
│   └── gui/                  # 前端代码 
├── CMakeLists.txt
├── .gitignore
└── README.md
```

## started
### Build by wasm
```
mkdir build_wasm && cd build_wasm
```

```
emcmake cmake -DBUILD_WASM=ON ..
make
```

### Build by python
```
mkdir build_pybind && cd build_pybind
```
```
cmake -DBUILD_PYTHON=ON ..
make
```

## log
### 4.12
目前完成了基本的Deflate压缩算法，在core中提供了```CompressorResult Compress(const std::vector<uint8_t>&)```的压缩 API 以及 ```CompressorResult Decompress(const std::vector<uint8_t>&)``` 的解压缩 API 

并且在```build_wasm/src/bindings```中构建了 html 前端


## API
### BitReader
- ```auto BitReader::ensureBits(uint8_t count) -> bool```
- ```auto BitReader::readBit(void) -> uint8_t```
- ```auto BitReader::readBits(uint8_t count) -> uint64_t```
- ```auto BitReader::readBytes(uint8_t* dst, size_t count) -> void```
- ```auto BitReader::changeSource(std::span<const uint8_t> source) -> void```
- ```auto BitReader::getByteRead(void) -> size_t```
- ```auto BitReader::getSourceSize(void) -> size_t```


### BitWriter
- ```auto BitWriter::ensureSpace(uint8_t count) -> bool```
- ```auto BitWriter::writeBit(uint8_t) -> void```
- ```auto BitWriter::writeBits(uint64_t value, uint8_t) -> void```
- ```auto writeBytes(const uint8_t* src, size_t count) -> size_t```
- ```auto BitWriter::flush() -> void```
- ```auto BitWriter::changeSource(std::span<uint8_t> source) -> void```
- ```auto BitWriter::getByteWritten(void) -> size_t```
- ```auto BitWriter::getSourceSize(void) -> size_t```