import director_basic.*
addpath('director_basic')

b = director_basic.Bar(3);
d = director_basic.MyClass();
c = MatClass();

cc = director_basic.MyClass.get_self(c);
dd = director_basic.MyClass.get_self(d);

bc = cc.cmethod(b);
bd = dd.cmethod(b);

cc.method(b);
if (c.cmethod ~= 7)
  error
end

if (bc.x ~= 34)
  error
end


if (bd.x ~= 16)
  error
end



