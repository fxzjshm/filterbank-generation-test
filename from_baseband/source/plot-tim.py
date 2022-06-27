
import sigpyproc.TimeSeries
import numpy
import sys
import seaborn
import matplotlib.pyplot

tim = sigpyproc.TimeSeries.TimeSeries.readTim(sys.argv[1])
matplotlib.pyplot.plot(tim)
matplotlib.pyplot.show()