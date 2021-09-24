import numpy as np
import matplotlib.pyplot as plt
import seaborn
import sys

filename = sys.argv[1]
nchans = sys.argv[2]
array = np.genfromtxt(filename, usecols = range(1,int(nchans)), delimiter=" ", autostrip=True)
map = seaborn.heatmap(array)
plt.show()
