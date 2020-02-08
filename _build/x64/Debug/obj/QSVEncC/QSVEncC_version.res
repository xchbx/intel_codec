        ÿÿ  ÿÿ                  :8  \   P E R F _ M O N I T O R _ S R C   P E R F _ M O N I T O R _ P Y W       0        ï»¿#!/usr/bin/env python
# -*- coding: utf-8 -*-
#  -----------------------------------------------------------------------------------------
#  QSVEnc by rigaya
#  -----------------------------------------------------------------------------------------
#  The MIT License
# 
#  Copyright (c) 2011-2016 rigaya
# 
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
# 
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
# 
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.
# 
#  ------------------------------------------------------------------------------------------
from pyqtgraph.Qt import QtGui, QtCore
import numpy as np
import pyqtgraph as pg
import time, re, threading
import statistics

DEFAULT_INTERVAL = 500
DEFAULT_KEEP_LENGTH = 60

tColorList = (
    (255,88,88),   #æ¡
    (255,255,255), #ç½
    (88,245,88),   #èç·
    (254,46,247),  #èãã³ã¯
    (0,252,255),   #æ°´è²
    (116,223,0),   #è¥è
    (4,180,134),   #éï½ç·
    (254,154,46),  #æ©
    (0,64,255),    #é
    (243,247,12),  #ã¬ã¢ã³
    (250,0,112),   #èµ¤ï½ãã³ã¯
    (200,254,46),  #é»ç·
    (104,138,8),   #è¥è(æ)
    (180,49,4),    #è¶
    (200,200,200), #ç°
    (172,91,245),  #ç´«
    (64,64,245),   #ç¾¤é
    (255,0,0)      #èµ¤
)

class PerfData:
    """
    åãã¼ã¿ãç®¡çãã
    aData ... ãã¼ã¿éå
    sName ... ãã¼ã¿å
    sUnit ... ãã¼ã¿ã®åä½
    nId   ... ãã¼ã¿ã®ã¤ã³ããã¯ã¹
    curve ... ã°ã©ãã«ç»é²ãããã¼ã¿æå ±
    plot  ... ãã®ãã¼ã¿ãä½¿ç¨ããã°ã©ãè»¸ãã¼ã¿
    bShow ... ãã®ãã¼ã¿ãè¡¨ç¤ºããã
    """
    aData = None
    sName = ""
    sUnit = ""
    nId = -1
    curve = None
    plot = None
    bShow = True

    def __init__(self, sName, sUnit, bShow):
        """
        sName ... ãã¼ã¿å
        sUnit ... ãã¼ã¿ã®åä½
        bShow ... ãã¼ã¿ãè¡¨ç¤ºãããã©ãã
        """
        assert isinstance(sName, str)
        assert isinstance(sUnit, str)
        self.aData    = []
        self.sName    = sName
        self.sUnit    = sUnit
        self.bShow    = bShow

class PerfYAxis:
    """
    yè»¸ã®æå ±ãç®¡çãã
    """
    ymin = 0
    ymax = 10
    firstData = 0 #ãã®è»¸ãä½¿ç¨ããæåã®ãã¼ã¿ã®ã¤ã³ããã¯ã¹
    sUnit = ""
    def __init__(self, ymin, ymax, sUnit, firstData):
        """
        ymin      ... åæã®yã®ä¸éå¤
        ymax      ... åæã®yã®ä¸éå¤
        sUnit     ... è»¸ã®åä½
        firstData ... ãã®è»¸ãä½¿ç¨ããæåã®ãã¼ã¿ã®ã¤ã³ããã¯ã¹
        """
        #åä½ã"%"ãªã 0% ï½ 100%ã®ç¯å²ãå¸¸ã«è¡¨ç¤ºããã°è¯ã
        self.ymin = 0   if sUnit == "%" else ymin
        self.ymax = 100 if sUnit == "%" else ymax
        self.sUnit = sUnit
        self.firstData = firstData

class PerfMonitor:
    """
    ã°ã©ãå¨ä½ãç®¡çãã
    aXdata         ... [float]    xè»¸ã®ãã¼ã¿
    aPerfData      ... [PerfData] yè»¸ã®æ§ããªãã¼ã¿
    dictYAxis      ... { str : PerfYAxis } åä½ç³»ãã¨ã®è»¸ãã¼ã¿
    win            ... ã°ã©ããã¼ã¿
    basePlot       ... è¦ªã¨ãªãè»¸
    xmin, xmax     ... xè»¸ã®ç¯å²
    xkeepLength    ... xè»¸ã®ãã¼ã¿ä¿æç¯å²
    nInputInterval ... èª­ã¿è¾¼ã¿éé
    timer          ... èª­ã¿è¾¼ã¿ç¨ã¿ã¤ãã¼
    nCheckRangeCount    ... Yè»¸ã®å¤åããã§ãã¯ããåæ°
    nCheckRangeInterval ... Yè»¸ã®å¤åããã§ãã¯éé
    """
    aXdata = []
    aPerfData = []
    dictYAxis = { }
    win = None
    basePlot = None
    xmax = 5
    xmin = 0
    xkeepLength = 30
    nInputInterval = 200
    timer = None
    nCheckRangeCount = 0
    nCheckRangeInterval = 4

    def __init__(self, nInputInterval, xkeepLength=30):
        self.nInputInterval = nInputInterval
        self.xkeepLength = xkeepLength

    def addData(self, prefData):
        assert isinstance(prefData, PerfData)
        prefData.nId = len(self.aPerfData)
        if prefData.bShow:
            #è²ãé¸æãã¦Penãçæ
            plotPen = pg.mkPen(color=tColorList[len(self.aPerfData)])
            if len(self.aPerfData) == 0:
                #ãã¼ã¿ãã²ã¨ã¤ããªãã¨ã
                #ã¾ãè»¸æå ±ãåæå
                perfDataYAxis = PerfYAxis(0, 10, prefData.sUnit, len(self.aPerfData))
                #ã¦ã£ã³ãã¦ãåæå
                self.win = pg.PlotWidget()
                pg.setConfigOptions(antialias = True)
                self.win.resize(540, 500)
                self.win.setWindowTitle('QSVEncC Performance Monitor')
                self.win.show()
                
                #è¦ªã¨ãã¦ä½æ
                prefData.plot = self.win.plotItem
                prefData.plot.showAxis('right')
                prefData.plot.setLabel('bottom', 'time', units='s')
                prefData.plot.setLabel('left', text=None, units=prefData.sUnit)
                prefData.plot.addLegend() #å¡ä¾ãè¡¨ç¤º(è¦ªã®è»¸ã«è¿½å ããããã¼ã¿ã¯å¡ä¾ã«èªåçã«è¿½å ããã)
                prefData.plot.showGrid(x = True, y = True) #ã¡ã¢ãªç·ãã°ã©ãåã«è¡¨ç¤º
                prefData.curve = prefData.plot.plot(x=self.aXdata, y=prefData.aData, name=prefData.sName) #ãã¼ã¿ãè¿½å 
                prefData.plot.setXRange(0, 15, update=False)
                prefData.plot.enableAutoRange('x', 0.95) #xè»¸ã¯èªåè¿½å¾
                prefData.plot.getAxis('left').enableAutoSIPrefix(False) #yè»¸ã®SIæ¥é ­è¾ã®ä»å ãç¦æ­¢

                #åä½ã"%"ãªãå¤åã0ï½100ã«åºå®ãã
                if prefData.sUnit == '%':
                    prefData.plot.setYRange(0, 100, 0, False)
                    prefData.plot.getAxis('left').setTickSpacing(25,12.5) #ç®çééãæå®
            
                #è¦ªã®è»¸ã¨ãã¦ç»é²
                self.basePlot = prefData.plot
                self.dictYAxis[prefData.sUnit] = perfDataYAxis
            else:
                if prefData.sUnit in self.dictYAxis:
                    #ãã§ã«åãåä½ã®è»¸ãå­å¨ããã°ãããã«è¿½å ãã
                    perfDataYAxis = self.dictYAxis[prefData.sUnit]
                    prefData.plot = self.aPerfData[perfDataYAxis.firstData].plot
                    #è¦ªã®è»¸ã®å ´åã«ã¯plotã«ã¯pg.PlotItemããããä»¥å¤ã®å ´åã«ã¯pg.ViewBoxãå¥ã£ã¦ããã®ã§æ³¨æ
                    #ãã¼ã¿ã®è¿½å æ¹æ³ãç°ãªã
                    if isinstance(self.aPerfData[perfDataYAxis.firstData].plot, pg.PlotItem):
                        prefData.curve = self.aPerfData[perfDataYAxis.firstData].plot.plot(x=self.aXdata, y=prefData.aData, name=prefData.sName)
                    else:
                        prefData.curve = pg.PlotCurveItem(x=self.aXdata, y=prefData.aData, name=prefData.sName)
                        prefData.plot.addItem(prefData.curve)
                else:
                    #ç°ãªãåä½ã®è»¸ãªããæ°ãã«è¿½å ãã
                    perfDataYAxis = PerfYAxis(0, 10, prefData.sUnit, len(self.aPerfData))
                    prefData.plot = pg.ViewBox()
                    if len(self.dictYAxis) >= 2:
                        #3ã¤ãä»¥éã®è»¸ã¯æ°ãã«ä½æããå¿è¦ããã
                        axItem = pg.AxisItem('right')
                        axItem.enableAutoSIPrefix(False) #è»¸ã®SIæ¥é ­è¾ã®ä»å ãç¦æ­¢
                        self.basePlot.layout.addItem(axItem, 2, len(self.dictYAxis)+1)
                        self.basePlot.scene().addItem(prefData.plot)
                        axItem.linkToView(prefData.plot)
                        prefData.plot.setXLink(self.basePlot)
                        axItem.setLabel(units=prefData.sUnit)
                    else:
                        self.basePlot.showAxis('right')
                        self.basePlot.scene().addItem(prefData.plot)
                        self.basePlot.getAxis('right').enableAutoSIPrefix(False) #è»¸ã®SIæ¥é ­è¾ã®ä»å ãç¦æ­¢
                        self.basePlot.getAxis('right').linkToView(prefData.plot)
                        prefData.plot.setXLink(self.basePlot)
                        self.basePlot.getAxis('right').setLabel(units=prefData.sUnit)
                    
                    self.dictYAxis[prefData.sUnit] = perfDataYAxis
                    prefData.curve = pg.PlotCurveItem(x=self.aXdata, y=prefData.aData, name=prefData.sName)
                    prefData.plot.addItem(prefData.curve)
                    prefData.plot.enableAutoRange('y', False)
                
                #ããã©ã«ãã®è»¸ä»¥å¤ã¯å¡ä¾ã«è¿½å ããã¦ããªãã®ã§ãããã«å¡ä¾ã«è¿½å 
                if prefData.plot != self.basePlot:
                    self.basePlot.legend.addItem(pg.PlotDataItem(pen=plotPen), prefData.sName)

            prefData.curve.setPen(plotPen)
        
        #ãã¼ã¿ãaPerfDataã«è¿½å 
        self.aPerfData.append(prefData)

        #ãã¼ã¿ãèª­ã¿è¾¼ãã¿ã¤ãã¼
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.run)
        self.timer.start(max(self.nInputInterval-25, 10))

    def parse_input_line(self, line):
        elems = line.rstrip().split(",")
        try:
            current_time = float(elems[0])
            self.aXdata.append(current_time)
            for i in range(1, len(elems)):
                value = float(elems[i])
                self.aPerfData[i-1].aData.append(value)
        except:
            pass

    def run(self):
        line = sys.stdin.readline()
        self.parse_input_line(line)

        #xè»¸ã®ç¯å²ãåå¾
        xmin = min(self.aXdata)
        xmax = max(self.aXdata)

        #æå®ä»¥ä¸ã«ç¯å²ãé·ããã°åé¤
        removeData = xmax - xmin > self.xkeepLength + 3
        if removeData:
            self.aXdata = self.aXdata[1:]
            xmin = min(self.aXdata)

        for data in self.aPerfData:
            assert isinstance(data, PerfData)
            if removeData:
                data.aData = data.aData[1:]

            if data.bShow:
                #åä½ã"%"ã®å ´åã¯ 0 - 100ã®åºå®ã§ãã
                if self.nCheckRangeCount == 0 and data.sUnit != "%":
                    #èªåã®åä½ç³»å¨ä½ã«ã¤ãã¦èª¿æ´
                    perfDataYAxis = self.dictYAxis[data.sUnit]
                    ymin = min(data.aData)
                    ymax = max(data.aData)
                    ymedian = statistics.median(data.aData)
                    perfDataYAxis.ymin = min(perfDataYAxis.ymin, ymin)
                    if perfDataYAxis.ymax < ymax or ymax * 2.0 < perfDataYAxis.ymax:
                        perfDataYAxis.ymax = ymax * 1.05
                        #ããç¨åº¦ãã¼ã¿ããã¾ã£ãããmedianã«ããä¸éãå¶éãã
                        if len(self.aXdata) >= 60:
                            perfDataYAxis.ymax = min(perfDataYAxis.ymax, ymedian * 8)
                        data.plot.setYRange(perfDataYAxis.ymin, perfDataYAxis.ymax, 0, False)

                #æ´æ°ããããã¼ã¿ãè¨­å®ãèªåçã«åæ ããã
                data.curve.setData(x=self.aXdata, y=data.aData)
                #
                if data.sUnit != self.aPerfData[0].sUnit:
                    data.plot.setGeometry(self.basePlot.vb.sceneBoundingRect())
                    data.plot.linkedViewChanged(self.basePlot.vb, data.plot.XAxis)
        
        #Yè»¸ã®å¤åãã§ãã¯ã¯4åã«ä¸åããè¡ããªã
        self.nCheckRangeCount = (self.nCheckRangeCount + 1) % self.nCheckRangeInterval

if __name__ == "__main__":
    import sys
    pg.mkQApp()

    nInterval = DEFAULT_INTERVAL
    nKeepLength = DEFAULT_KEEP_LENGTH

    #ã³ãã³ãã©ã¤ã³å¼æ°ãåãåã
    iargc = 1
    while iargc < len(sys.argv):
        if sys.argv[iargc] == "-i":
            iargc += 1
            try:
                nInterval = int(sys.argv[iargc])
            except:
                nInterval = DEFAULT_INTERVAL
        if sys.argv[iargc] == "-xrange":
            iargc += 1
            try:
                nKeepLength = int(sys.argv[iargc])
            except:
                nKeepLength = DEFAULT_KEEP_LENGTH
        iargc += 1

    monitor = PerfMonitor(nInterval, nKeepLength)

    #ãããã¼è¡ãèª­ã¿è¾¼ã¿
    line = sys.stdin.readline()
    elems = line.rstrip().split(",")

    #"()"åããåä½ãã¨ãã¦æ½åºããããã®æ­£è¦è¡¨ç¾
    r = re.compile(r'(.*)\s\((.+)\)')
    for counter in elems[1:]:
        #"()"åããåä½ãã¨ãã¦æ½åº
        m = r.search(counter)
        unit = "" if m == None else m.group(2)
        #name = "" if m == None else m.group(1)
        #ãã¼ã¿ã¨ãã¦è¿½å  (åä½ãªããå¹³åã¯è¡¨ç¤ºããªã)
        monitor.addData(PerfData(counter, unit, m != None))
    
    #å¡ä¾ãä½æ
    counter_names = []
    for data in monitor.aPerfData:
        if data.bShow:
            counter_names.append(data.sName)

    if (sys.flags.interactive != 1) or not hasattr(QtCore, 'PYQT_VERSION'):
        QtGui.QApplication.instance().exec_()  ¬      ÿÿ ÿÿ     0         ¬4   V S _ V E R S I O N _ I N F O     ½ïþ               ?                        
   S t r i n g F i l e I n f o   æ   0 4 1 1 0 4 b 0   X    C o m m e n t s   b a s e d   o n   I n t e l   M e d i a   S D K   S a m p l e        C o m p a n y N a m e      -  F i l e D e s c r i p t i o n     Q S V E n c C   ( x 6 4 )   -   Q u i c k S y n c V i d e o   E n c o d e r   ( C U I )     *   F i l e V e r s i o n     3 . 2 5     z -  I n t e r n a l N a m e   Q S V E n c C   ( x 6 4 )   -   Q u i c k S y n c V i d e o   E n c o d e r   ( C U I )     @   L e g a l C o p y r i g h t   Q S V E n c C @ h a p p y   @   O r i g i n a l F i l e N a m e   Q S V E n c C . e x e   z -  P r o d u c t N a m e     Q S V E n c C   ( x 6 4 )   -   Q u i c k S y n c V i d e o   E n c o d e r   ( C U I )     .   P r o d u c t V e r s i o n   3 . 2 5     D    V a r F i l e I n f o     $    T r a n s l a t i o n     °