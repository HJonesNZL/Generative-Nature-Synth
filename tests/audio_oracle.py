"""Independent direct-equation M2 reference and RIFF reader (test-only)."""
import math
import struct


def sample(age, rate, frequency, attack=.008, decay=.650, gain_db=0):
    a, d, r = math.ceil(attack*rate), math.ceil(decay*rate), math.ceil(.020*rate)
    if age < 0 or age >= a+d+r:
        return 0.0
    attack_gain = (1-math.cos(math.pi*age/a))/2 if age < a else 1
    decay_gain = math.exp(-math.log(1000)*max(0,age-a)/d)
    release_gain = (1+math.cos(math.pi*(age-a-d)/r))/2 if age >= a+d else 1
    phase = 2*math.pi*frequency*age/rate
    wave = (math.sin(phase)+.25*math.sin(2*phase)+.08*math.sin(3*phase))/1.33
    return .05*attack_gain*decay_gain*release_gain*wave*10**(gain_db/20)


def read_wave(path):
    data=path.read_bytes()
    if len(data)<12 or data[:4]!=b'RIFF' or data[8:12]!=b'WAVE' or struct.unpack_from('<I',data,4)[0]+8!=len(data):
        raise ValueError('Invalid RIFF extent')
    chunks={}; position=12
    while position<len(data):
        if len(data)-position<8: raise ValueError('Truncated chunk header')
        tag=data[position:position+4]; size=struct.unpack_from('<I',data,position+4)[0]
        end=position+8+size
        if end+(size%2)>len(data) or tag in chunks: raise ValueError('Invalid chunk')
        chunks[tag]=data[position+8:end]; position=end+(size%2)
    if not {b'fmt ', b'fact', b'data', b'LIST'} <= chunks.keys():
        raise ValueError('Missing required WAV chunk')
    fmt=chunks[b'fmt ']
    if len(fmt)!=40: raise ValueError('Invalid format size')
    fields=struct.unpack_from('<HHIIHHHHI',fmt)
    if len(fmt)!=40 or fields[0:2]!=(65534,2) or fields[4:9]!=(6,24,22,24,3):
        raise ValueError('Not expected stereo 24-bit extensible PCM')
    if fmt[24:]!=bytes.fromhex('0100000000001000800000aa00389b71'): raise ValueError('PCM subtype')
    rate=fields[2]
    if fields[3]!=rate*6: raise ValueError('Byte rate')
    raw=chunks[b'data']
    if len(raw)%6: raise ValueError('Partial frame')
    frames=len(raw)//6
    if len(chunks[b'fact'])!=4 or struct.unpack('<I',chunks[b'fact'])[0]!=frames: raise ValueError('Fact length')
    pcm=[int.from_bytes(raw[i:i+3],'little',signed=True)/8388607 for i in range(0,len(raw),3)]
    info=chunks[b'LIST']
    if len(info)<12 or info[:8]!=b'INFOICMT': raise ValueError('Missing recipe')
    size=struct.unpack_from('<I',info,8)[0]
    if size<1 or 12+size+(size%2)!=len(info) or info[11+size]!=0:
        raise ValueError('Invalid INFO comment extent or terminator')
    import json
    recipe=json.loads(info[12:12+size].rstrip(b'\0'))
    return rate,pcm,recipe,raw
