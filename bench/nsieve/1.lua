-- nsieve benchmark
local function nsieve(m)
    local flags = {}
    for i = 2, m do flags[i] = true end
    local count = 0
    for i = 2, m - 1 do
        if flags[i] then
            count = count + 1
            for j = i + i, m - 1, i do
                flags[j] = false
            end
        end
    end
    io.write(string.format("Primes up to %8d %8d\n", m, count))
end

local N = tonumber(arg and arg[1]) or 4
for i = 0, 2 do
    nsieve(10000 * (2 ^ (N - i)))
end
