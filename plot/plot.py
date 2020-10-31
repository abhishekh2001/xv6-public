import matplotlib.pyplot as plt

file = open("res.txt", "r")

while True:
    line = file.readline()
    if not line:
        break

    if line[0:4] == "PLOT":
        spacesep = line.split()
        print(line)
        pid = int(spacesep[1])
        q = int(spacesep[2])
        ticks = int(spacesep[3])
        plt.scatter(ticks, q, pid)
        # plt.plot(ticks, q, pid)

plt.show()