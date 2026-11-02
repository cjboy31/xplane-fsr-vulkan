"""Embed the independently compiled native renderer input/history kernels."""
import pathlib, struct, sys
target = pathlib.Path(sys.argv[1])
text = '#pragma once\n#include <cstdint>\n'
for symbol, filename in zip(('nativeMotion2D', 'nativeMotionArray', 'nativeMotionHistory'), sys.argv[2:]):
    data = pathlib.Path(filename).read_bytes()
    words = struct.unpack(f'<{len(data)//4}I', data)
    text += f'static const uint32_t {symbol}[]={{\n'
    text += ',\n'.join(','.join(f'0x{x:08x}u' for x in words[i:i+8]) for i in range(0,len(words),8))
    text += '\n};\n'
target.write_text(text)
