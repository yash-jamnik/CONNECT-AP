import re

txt = open("test.txt","r").read()
nums = re.findall(r'0[xX]([0-9A-Fa-f]{1,2})', txt)
data = bytes(int(x,16) for x in nums)

open("batman1.bin","wb").write(data)

print("bytes written:", len(data))