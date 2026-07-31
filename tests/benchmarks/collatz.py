steps = 0
number = 1
while number < 1000000:
    current = number
    while current > 1:
        steps += 1
        remainder = current % 2
        current = current // 2 if remainder == 0 else (current * 3) + 1
    number += 1
print(steps)
