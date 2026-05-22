import sys
sys.path.insert(0, 'src')
from gui.engine.token_parser import (
    _memory_compress_bytes_for_demo, _CanonicalHuffmanTree, _LSBBitReader
)
from gui.engine.compressor import AlgorithmType
from gui.engine.file_protocol import prepare_token_parse_payload

with open('resources/wow.html', 'rb') as f:
    raw_data = f.read()

compressed = _memory_compress_bytes_for_demo(AlgorithmType.DEFLATE, raw_data, None)
payload = prepare_token_parse_payload(compressed, AlgorithmType.DEFLATE)

triple_count = int.from_bytes(payload[0:4], 'little')
lit_sz = int.from_bytes(payload[4:6], 'little')
off_sz = int.from_bytes(payload[6:8], 'little')
len_sz = int.from_bytes(payload[8:10], 'little')
header_end = 10 + lit_sz + off_sz + len_sz

print(f'triple_count={triple_count} lit_sz={lit_sz} off_sz={off_sz} len_sz={len_sz}')
print(f'header_end={header_end} total={len(payload)}')

lit_tree = _CanonicalHuffmanTree(payload[10:10 + lit_sz])
off_tree = _CanonicalHuffmanTree(payload[10 + lit_sz:10 + lit_sz + off_sz])
len_tree = _CanonicalHuffmanTree(payload[10 + lit_sz + off_sz:header_end])

print(f'lit_tree reverse_map entries={len(lit_tree.reverse_map)}')
print(f'lit_tree code_lengths={dict(sorted(lit_tree.code_lengths.items()))}')
print(f'off_tree reverse_map entries={len(off_tree.reverse_map)}')
print(f'len_tree reverse_map entries={len(len_tree.reverse_map)}')

search_size = 4096
lookahead = 256
offset_bits = max(1, search_size.bit_length())
length_bits = max(1, lookahead.bit_length())
offset_chunk = 8
length_chunk = 8
print(f'offset_bits={offset_bits} length_bits={length_bits}')

token_data = payload[header_end:]
reader = _LSBBitReader(token_data)
print(f'token_data size={len(token_data)} first10={token_data[:10].hex()}')

cursor = 0
for ti in range(min(triple_count, 10)):
    bit_start = reader.bit_position()
    if not reader.ensure(1):
        print(f'  ti={ti}: ensure(1) failed at bit={reader.bit_position()}')
        break
    is_literal = reader.read_bit()
    if is_literal:
        lit_bit_start = reader.bit_position()
        sym = lit_tree.decode(reader)
        if sym is None:
            print(f'  ti={ti}: lit_tree.decode FAILED at bit={reader.bit_position()} cursor={cursor}')
            break
        lit_bits = reader.bit_position() - lit_bit_start
        ch = chr(sym) if 32 <= sym < 127 else '?'
        print(f'  ti={ti}: LITERAL sym={sym}({ch}) bits={lit_bits} cursor={cursor}')
        cursor += 1
    else:
        num_chunks = (offset_bits + offset_chunk - 1) // offset_chunk
        last_chunk_bits = offset_bits % offset_chunk or offset_chunk
        offset = 0
        off_ok = True
        for ci in range(num_chunks):
            chunk = off_tree.decode(reader)
            if chunk is None:
                print(f'  ti={ti}: off_tree.decode chunk{ci} FAILED at bit={reader.bit_position()}')
                off_ok = False
                break
            if ci == num_chunks - 1 and last_chunk_bits < offset_chunk:
                chunk &= (1 << last_chunk_bits) - 1
            offset |= chunk << (ci * offset_chunk)
        if not off_ok:
            break

        num_chunks = (length_bits + length_chunk - 1) // length_chunk
        last_chunk_bits = length_bits % length_chunk or length_chunk
        length = 0
        len_ok = True
        for ci in range(num_chunks):
            chunk = len_tree.decode(reader)
            if chunk is None:
                print(f'  ti={ti}: len_tree.decode chunk{ci} FAILED at bit={reader.bit_position()}')
                len_ok = False
                break
            if ci == num_chunks - 1 and last_chunk_bits < length_chunk:
                chunk &= (1 << last_chunk_bits) - 1
            length |= chunk << (ci * length_chunk)
        if not len_ok:
            break

        token_bits = reader.bit_position() - bit_start
        print(f'  ti={ti}: MATCH offset={offset} length={length} bits={token_bits} cursor={cursor}')
        cursor += length

    print(f'  ti={ti}: bit_pos={reader.bit_position()} byte_pos={reader._byte_pos} cursor={cursor}')

print(f'\nfinal: cursor={cursor} total_tokens_processed={min(ti+1, triple_count)}')