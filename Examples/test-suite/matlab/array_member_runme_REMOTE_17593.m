f = array_member.Foo.default;
f.data = array_member.global_data.default;

for i=0:7,
    if (array_member.get_value(f.data,i) ~= array_member.get_value(array_member.global_data,i))
      error('Bad array assignment');
    end
end

for i=0:7,
    array_member.set_value(f.data,i,-i);
end

global_data = f.data;

for i=0:7,
  if (array_member.get_value(f.data,i) ~= array_member.get_value(global_data,i))
    error('Bad array assignment')
  end
end
