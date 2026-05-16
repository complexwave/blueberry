-- map benchmark: 80 keys, 10 maps rotating, cache pressure
local N = tonumber(arg and arg[1]) or 10000000

local maps = {}
for j = 1, 1000 do
    local m = {}
    m.k00 = 1;  m.k01 = 2;  m.k02 = 3;  m.k03 = 4;  m.k04 = 5
    m.k05 = 6;  m.k06 = 7;  m.k07 = 8;  m.k08 = 9;  m.k09 = 10
    m.k10 = 11; m.k11 = 12; m.k12 = 13; m.k13 = 14; m.k14 = 15
    m.k15 = 16; m.k16 = 17; m.k17 = 18; m.k18 = 19; m.k19 = 20
    m.k20 = 21; m.k21 = 22; m.k22 = 23; m.k23 = 24; m.k24 = 25
    m.k25 = 26; m.k26 = 27; m.k27 = 28; m.k28 = 29; m.k29 = 30
    m.k30 = 31; m.k31 = 32; m.k32 = 33; m.k33 = 34; m.k34 = 35
    m.k35 = 36; m.k36 = 37; m.k37 = 38; m.k38 = 39; m.k39 = 40
    m.k40 = 41; m.k41 = 42; m.k42 = 43; m.k43 = 44; m.k44 = 45
    m.k45 = 46; m.k46 = 47; m.k47 = 48; m.k48 = 49; m.k49 = 50
    m.k50 = 51; m.k51 = 52; m.k52 = 53; m.k53 = 54; m.k54 = 55
    m.k55 = 56; m.k56 = 57; m.k57 = 58; m.k58 = 59; m.k59 = 60
    m.k60 = 61; m.k61 = 62; m.k62 = 63; m.k63 = 64; m.k64 = 65
    m.k65 = 66; m.k66 = 67; m.k67 = 68; m.k68 = 69; m.k69 = 70
    m.k70 = 71; m.k71 = 72; m.k72 = 73; m.k73 = 74; m.k74 = 75
    m.k75 = 76; m.k76 = 77; m.k77 = 78; m.k78 = 79; m.k79 = 80
    maps[j] = m
end

local sum = 0
local mi = 1
for i = 1, N do
    local m = maps[mi]
    mi = mi + 1
    if mi > 1000 then mi = 1 end

    sum = sum + m.k00 + m.k01 + m.k02 + m.k03 + m.k04
    sum = sum + m.k05 + m.k06 + m.k07 + m.k08 + m.k09
    sum = sum + m.k10 + m.k11 + m.k12 + m.k13 + m.k14
    sum = sum + m.k15 + m.k16 + m.k17 + m.k18 + m.k19
    sum = sum + m.k20 + m.k21 + m.k22 + m.k23 + m.k24
    sum = sum + m.k25 + m.k26 + m.k27 + m.k28 + m.k29
    sum = sum + m.k30 + m.k31 + m.k32 + m.k33 + m.k34
    sum = sum + m.k35 + m.k36 + m.k37 + m.k38 + m.k39
    sum = sum + m.k40 + m.k41 + m.k42 + m.k43 + m.k44
    sum = sum + m.k45 + m.k46 + m.k47 + m.k48 + m.k49
    sum = sum + m.k50 + m.k51 + m.k52 + m.k53 + m.k54
    sum = sum + m.k55 + m.k56 + m.k57 + m.k58 + m.k59
    sum = sum + m.k60 + m.k61 + m.k62 + m.k63 + m.k64
    sum = sum + m.k65 + m.k66 + m.k67 + m.k68 + m.k69
    sum = sum + m.k70 + m.k71 + m.k72 + m.k73 + m.k74
    sum = sum + m.k75 + m.k76 + m.k77 + m.k78 + m.k79

    i = i + 1
end

print(sum)
