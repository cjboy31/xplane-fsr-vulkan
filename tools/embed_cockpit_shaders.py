import pathlib,struct,sys
source=pathlib.Path(sys.argv[1]);result='#pragma once\n#include <cstdint>\n'
for name,path in zip(('cockpitRestore16Spv','cockpitRestore32Spv'),sys.argv[2:]):
    data=pathlib.Path(path).read_bytes();words=struct.unpack('<%dI'%(len(data)//4),data)
    result+='static const uint32_t '+name+'[]={\n'+',\n'.join(','.join('0x%08xu'%v for v in words[i:i+8]) for i in range(0,len(words),8))+'\n};\n'
(source/'cockpit_restore_spv.hpp').write_text(result)
