class Greeter
  def greet
    puts "Hello World!"
  end
end
greeter = Greeter.new

while true
  greeter.greet
  sleep 1
end
