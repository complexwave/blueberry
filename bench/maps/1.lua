-- map/prototype chain access benchmark
-- heavy on nested field lookups via metatables, light on function calls

local N = tonumber(arg and arg[1]) or 10000000

-- build a 3-level prototype chain using metatables
local base = {}
base.x = 1
base.y = 2
base.z = 3

local mid = {}
mid.a = 10
mid.b = 20
mid.c = 30
setmetatable(mid, { __index = base })

local leaf = {}
leaf.p = 100
leaf.q = 200
leaf.r = 300
setmetatable(leaf, { __index = mid })

-- nested map: leaf.data.inner.value
leaf.data = { inner = { value = 42, extra = { deep = 7 } } }

local sum = 0
for i = 1, N do
    --[[ prototype chain lookups (inherited fields)
    sum = sum + leaf.x
    sum = sum + leaf.y
    sum = sum + leaf.z
    sum = sum + leaf.a
    sum = sum + leaf.b
    sum = sum + leaf.c
    ]]
    
    -- own fields
    sum = sum + leaf.p
    sum = sum + leaf.q
    sum = sum + leaf.r

     sum = sum + leaf.p
    sum = sum + leaf.q
    sum = sum + leaf.r
    
     sum = sum + leaf.p
    sum = sum + leaf.q
    sum = sum + leaf.r
    
     sum = sum + leaf.p
    sum = sum + leaf.q
    sum = sum + leaf.r
end

print(sum)
