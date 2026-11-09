.data
.byte 1,2,3,4
.p2alignl 3, 0xdeadbeef
.byte 2,2
.balignw 4, 0x1234
.text
nop
.balignw 16, 0x1234
.balignl 32, 0xd503201f
.p2alignw 6
