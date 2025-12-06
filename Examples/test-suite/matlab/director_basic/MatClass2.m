classdef MatClass2 < handle

  methods
        function [] = method(obj,vptr)
           disp(12345);
           obj.cmethod = 7;
        end
        function out = vmethod(obj,b)
            b.x = b.x + 31;
            out=b;
        end
  end
end

