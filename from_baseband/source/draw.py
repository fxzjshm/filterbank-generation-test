#!/usr/bin/env python3

import numpy as np
import matplotlib.pyplot as plt
import seaborn
import sys

filename = sys.argv[1]
array = np.genfromtxt(filename, delimiter=' ', autostrip=True)

# print(len(array))
if array.ndim == 1:
    plt.plot(array)
else:
    map = seaborn.heatmap(array)
    #plt.colorbar()
plt.show()
