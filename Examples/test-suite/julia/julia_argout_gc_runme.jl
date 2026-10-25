include(joinpath(@__DIR__, "julia_argout_gc.jl"))
using .julia_argout_gc

# swig -julia -c++ julia_argout_gc.i; build libjulia_argout_gc_wrap against julia.h
junk = Any[]
for i in 1:200
    n = 1 + i % 7
    v1, v2, a, b, v3 = split3(n)
    @assert v1 == fill(1.0, n) && v2 == fill(2.0, n) && v3 == fill(3.0, n)
    @assert a == n && b == 2n
    r, w1, c, w2 = ret_split(n)
    @assert r == fill(7.0, n) && w1 == fill(1.0, n) && c == n && w2 == fill(2.0, n)
    try
        split_fail()
        error("split_fail did not throw")
    catch e
        @assert e isa julia_argout_gc.SwigError
    end
    push!(junk, [string(j) for j in 1:n]); length(junk) > 500 && empty!(junk)
end
println("julia_argout_gc: ok")
