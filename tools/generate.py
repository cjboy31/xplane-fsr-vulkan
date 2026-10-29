"""Generate shader words and sizes for safely copying known Vulkan pNext nodes."""
import pathlib,struct,sys,xml.etree.ElementTree as ET
source,headers,binary=map(pathlib.Path,sys.argv[1:4])
data=binary.read_bytes()
words=struct.unpack('<%dI'%(len(data)//4),data)
(source/'temporal_spv.hpp').write_text('#pragma once\n#include <cstdint>\nstatic const uint32_t temporalSpv[]={\n'+',\n'.join(','.join('0x%08xu'%v for v in words[i:i+8]) for i in range(0,len(words),8))+'\n};\n')
if len(sys.argv)>4:
    msdata=pathlib.Path(sys.argv[4]).read_bytes()
    mswords=struct.unpack('<%dI'%(len(msdata)//4),msdata)
    (source/'temporal_msaa_spv.hpp').write_text('#pragma once\n#include <cstdint>\nstatic const uint32_t temporalMsaaSpv[]={\n'+',\n'.join(','.join('0x%08xu'%v for v in mswords[i:i+8]) for i in range(0,len(mswords),8))+'\n};\n')
core=(headers/'include/vulkan/vulkan_core.h').read_text()
root=ET.parse(headers/'registry/vk.xml').getroot()
cases=[]
seen=set()
for t in root.findall('./types/type'):
    name=t.get('name')
    if t.get('category')!='struct' or not name or t.get('alias') or 'typedef struct '+name+' {' not in core:continue
    member=t.find('./member[name="sType"]')
    if member is None:continue
    value=member.get('values')
    if not value or ',' in value or value in seen:continue
    seen.add(value);cases.append('case %s:return sizeof(%s);'%(value,name))
(source/'feature_sizes.hpp').write_text('#pragma once\n#include <cstddef>\nstatic size_t featureStructureSize(VkStructureType type){switch(type){\n'+'\n'.join(cases)+'\ndefault:return 0;}}\n')
print('Generated shader (%d words), %d Vulkan structure sizes'%(len(words),len(cases)))
