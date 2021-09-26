#!/usr/bin/env python3

import numpy as np
import matplotlib.pyplot as plt
import seaborn
import sys

filename = sys.argv[1]
col_l = sys.argv[2]
col_r = sys.argv[3]
array = np.genfromtxt(filename, usecols = range(int(col_l),int(col_r)), delimiter=" ", autostrip=True)
map = seaborn.heatmap(array)
plt.show()
