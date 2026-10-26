"""Independent, disposable exFAT fixtures from the Microsoft on-disk spec."""
import math
import struct

def checksum(data, bits=32, skip=()):
    result=0;mask=(1<<bits)-1
    for i,value in enumerate(data):
        if i not in skip:result=(((result>>1)|(result<<(bits-1)))+value)&mask
    return result

def entry_checksum(image, at):
    size=(image[at+1]+1)*32
    struct.pack_into('<H',image,at+2,checksum(image[at:at+size],16,(2,3)))

def make_exfat(contents, *, nofat=False, directories=()):
    cluster_size=4096;nodes={'':{'children':[], 'directory':True}}
    for directory in sorted(directories):
        parent=''
        for component in directory.split('/'):
            name=parent+'/'+component if parent else component
            if name not in nodes:
                nodes[name]={'children':[], 'directory':True};nodes[parent]['children'].append(name)
            parent=name
    for path,body in sorted(contents.items()):
        parent=''
        for component in path.split('/')[:-1]:
            name=parent+'/'+component if parent else component
            if name not in nodes:
                nodes[name]={'children':[], 'directory':True};nodes[parent]['children'].append(name)
            parent=name
        nodes[path]={'body':body,'directory':False};nodes[parent]['children'].append(path)
    upcase=[]
    for i in range(65536):
        upper=chr(i).upper();upcase.append(ord(upper) if len(upper)==1 and ord(upper)<=65535 else i)
    table=struct.pack('<65536H',*upcase)
    next_cluster=35
    for path,node in nodes.items():
        if node['directory']:
            entries=3 if not path else 0
            for child in node['children']:
                units=len(child.rsplit('/',1)[-1].encode('utf-16-le'))//2
                entries+=2+math.ceil(units/15)
            node['length']=math.ceil((entries+1)*32/cluster_size)*cluster_size
        else:node['length']=len(node['body'])
        node['clusters']=math.ceil(node['length']/cluster_size)
        node['first']=next_cluster if node['clusters'] else 0;next_cluster+=node['clusters']
    count=next_cluster-2+8;fat_sectors=math.ceil((count+2)*4/512);heap=24+fat_sectors
    volume=heap+count*8;image=bytearray(volume*512)
    boot=bytearray(12*512);boot[:11]=b'\xeb\x76\x90EXFAT   '
    struct.pack_into('<QQIIIIIIHHBBBBB',boot,64,0,volume,24,fat_sectors,heap,count,nodes['']['first'],1,0x100,0,9,3,1,0x80,0)
    struct.pack_into('<H',boot,510,0xaa55)
    for i in range(1,9):struct.pack_into('<I',boot,(i+1)*512-4,0xaa550000)
    value=checksum(boot[:11*512],skip=(106,107,112));boot[11*512:]=struct.pack('<I',value)*128
    image[:len(boot)]=boot;image[12*512:24*512]=boot
    def base(first):return heap*512+(first-2)*cluster_size
    struct.pack_into('<II',image,24*512,0xfffffff8,0xffffffff)
    def chain(first,n):
        for i in range(n):struct.pack_into('<I',image,24*512+(first+i)*4,first+i+1 if i+1<n else 0xffffffff)
    chain(2,1);chain(3,32)
    bitmap=bytearray(math.ceil(count/8))
    for i in range(next_cluster-2):bitmap[i//8]|=1<<(i%8)
    image[base(2):base(2)+len(bitmap)]=bitmap;image[base(3):base(3)+len(table)]=table
    locations={}
    for path,node in nodes.items():
        chain(node['first'],node['clusters'])
        if not node['directory']:
            if node['length']:image[base(node['first']):base(node['first'])+node['length']]=node['body']
            continue
        at=base(node['first'])
        if not path:
            image[at]=0x83;image[at+32]=0x81
            struct.pack_into('<IQ',image,at+32+20,2,len(bitmap));image[at+64]=0x82
            struct.pack_into('<I',image,at+64+4,checksum(table));struct.pack_into('<IQ',image,at+64+20,3,len(table));at+=96
        for child in node['children']:
            item=nodes[child];name=child.rsplit('/',1)[-1].encode('utf-16-le');units=list(struct.unpack('<'+'H'*(len(name)//2),name))
            names=math.ceil(len(units)/15);data=bytearray((names+2)*32);data[0]=0x85;data[1]=names+1
            struct.pack_into('<H',data,4,16 if item['directory'] else 32);data[32]=0xc0
            data[33]=(3 if nofat else 1) if item['length'] else 0;data[35]=len(units)
            folded=struct.pack('<'+'H'*len(units),*(upcase[x] for x in units));struct.pack_into('<H',data,36,checksum(folded,16))
            struct.pack_into('<Q',data,40,item['length']);struct.pack_into('<IQ',data,52,item['first'],item['length'])
            for i in range(names):data[64+i*32]=0xc1;chunk=name[i*30:(i+1)*30];data[66+i*32:66+i*32+len(chunk)]=chunk
            struct.pack_into('<H',data,2,checksum(data,16,(2,3)));image[at:at+len(data)]=data;locations[child]=at;at+=len(data)
    return image,{'entries':locations,'fat':24*512,'heap':heap*512,'root':base(nodes['']['first']),'bitmap':base(2),'upcase':base(3),'nodes':nodes}
