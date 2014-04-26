import sys
import time
import math
import array
import Image
import math
import pickle
import zlib

import gameduino2 as gd2
import gameduino2.convert

def readz80(z80file):
    z80 = array.array('B', open(z80file, "rb").read()).tolist()
    mem = []
    comp = z80[30:]
    while True:
      if comp == [0, 0xed, 0xed, 0]:
        break
      if comp[0:2] == [0xed, 0xed]:
        mem += [comp[3]] * comp[2]
        comp = comp[4:]
      else:
        mem.append(comp.pop(0))
    assert len(mem) == 0xc000
    return array.array('B', mem)

def color(a, bright):
    if bright:
        mx = 255
    else:
        mx = 0xaa
    g = mx * ((a & 4) != 0)
    r = mx * ((a & 2) != 0)
    b = mx * ((a & 1) != 0)
    return (r,g,b)

def bicolor(im, a):
    ink = color(a & 7, a & 64)
    paper = color((a >> 3) & 7, a & 64)
    im_paper = Image.new("RGB", im.size, ink)
    im_ink = Image.new("RGB", im.size, paper)
    return Image.composite(im_paper, im_ink, im)

# All this is taken from
# http://www.icemark.com/dataformats/manic/mmformat.htm

def img8x8(d):
    i8 = Image.fromstring("1", (8,8), d[:8].tostring())
    r = Image.new("1", (16,16))
    r.paste(i8, (0,0))
    return r

def img16x16(d):
    return Image.fromstring("1", (16,16), d.tostring())

class Level:
    def __init__(self, id, lvl):
        """
        background  512-char background image
        bgchars     8 character graphics
        bgattr      8 character attributes
        border      border color
        item        item sprite, 16x16 1-color image
        items       up to 5 items (x,y)
        portalattr  portal attribute byte
        portal      portal 16x16 graphic
        portalxy    portal position (x,y)
        guardian    8 16x16 guardian images, as 32-byte arrays
        hguardians  up to 4 h-guardians (attr, x, y, d, x0, x1)
        """

        self.id = id 
        self.name = lvl[512:512+32].tostring().strip()
        a2c = {}    # attribute to c mapping
        self.bgchars = [None] * 8
        self.bgattr = [None] * 8
        self.bgpal = [None] * 8
        for c in range(8):
            o = 544 + 9 * c
            attr = lvl[o]
            data = lvl[o+1:o+9]
            if not attr in a2c:
                a2c[attr] = c
            self.bgchars[c] = data
            self.bgattr[c] = attr
            if c == 4:  # conveyor
                conv_top = data[0]
                conv_bot = data[3]
        self.background = array.array('B', [a2c[a] for a in lvl[:512]])
        self.border = gd2.RGB(*color(lvl[627], 0))
        self.item = lvl[692:700]
        self.items = []
        def getxy(dd):
            """ decode a packed screen coordinate """
            x = dd[0] & 31
            y = (dd[0] >> 5) + ((dd[1] & 1) << 3)
            return (8 * x, 8 * y)

        for i in range(5):
            idata = lvl[629 + 5*i:629 + 5 * (i+1)]
            if idata[0] == 255:
                break
            if idata[0]:
                (x, y) = getxy(idata[1:3])
                self.items.append((x, y))

        self.portalattr = lvl[655]
        self.portal = lvl[656:688]
        self.portalxy = getxy(lvl[688:690])
        self.willyxy = getxy(lvl[620:622])
        self.willyxy = (self.willyxy[0], self.willyxy[1] + ((lvl[616] >> 1) & 7))
        self.willyd = lvl[618];
        self.willyf = lvl[617];

        self.guardian = [lvl[768+32*i:768+32*(i+1)] for i in range(8)]

        self.hguardians = [(0,0,0,0,0,0)] * 8
        for i in range(4):
            gdata = lvl[702 + 7*i: 702 + 7*(i+1)]
            if gdata[0] == 255:
                break
            if gdata[0]:
                a = gdata[0]
                x,y = getxy(gdata[1:3])
                d = gdata[4]
                x0 = 8 * (gdata[5] & 0x1f)
                x1 = 8 * (gdata[6] & 0x1f)
                assert x0 < x1
                self.hguardians[i] = (a, x, y, d, x0, x1)
        if self.id != 4:    # special for Eugene's lair
            for i in range(4):
                gdata = lvl[733 + 7*i: 733 + 7*(i+1)]
                if gdata[0] == 255:
                    break
                if gdata[0]:
                    a = gdata[0]
                    y = gdata[2]
                    x = gdata[3] * 8
                    d = gdata[4]
                    y0 = gdata[5]
                    y1 = gdata[6]
                    self.hguardians[4+i] = (a, x, y, d, y0, y1)
        self.conveyordir = 1 - lvl[623] # Game uses 1=right here, sigh
        self.air = 8 * ((lvl[700] - 32) - 4) + len([c for c in bin(lvl[701]) if (c == '1')])
        self.special = lvl[736:768]
        assert (0 < self.air <= 224)
        print self.name

    def background_tiles(self):
        im0 = Image.new("RGB", (8, 8 * 15))
        extchars = [c for c in self.bgchars]
        extattrs = [c for c in self.bgattr]
        floortile = self.bgchars[2].tostring()
        for crumble in range(1, 8):
            tile = ((chr(0) * crumble) + floortile)[:8]
            extchars.append(tile)
            extattrs.append(self.bgattr[2])
        tiles = []
        for i,(t,a) in enumerate(zip(extchars, extattrs)):
            mtile = Image.fromstring("1", (8,8), t)
            tile = bicolor(mtile, a)
            im0.paste(tile, (0, 8 * i))
            tiles.append(tile)
        return tiles
        
    def background_str(self):
        return self.background.tostring()

    def item_image(self):
        return Image.fromstring("1", (8, 8), self.item.tostring())

    def portal_image(self):
        im = Image.fromstring("1", (16, 16), self.portal.tostring())
        im = bicolor(im, self.portalattr)
        return im

    def special_image(self):
        im = Image.fromstring("1", (16, 16), self.special.tostring())
        return im

    def guardian_images(self):
        return [Image.fromstring("1", (16, 16), g.tostring()) for g in self.guardian]

    def dump(self, hh):
        print >>hh, "{ //", self.name
        def init(a):
            return ",".join(["%d" % v for v in a])
        def da(a):
            print >>hh, "{"
            print >>hh, init(a)
            print >>hh, "},"

        # da(array.array('B', self.name))
        print >>hh, '"%s",' % self.name
        print >>hh, "0x%06x," % self.border
        # da(self.background)
        # print >>hh, "{", ",".join([init(cc) for cc in self.bgchars]), "},"
        # da(self.bgattr)
        # da(self.item)
        print >>hh, "{", ",".join(["{%d,%d}" % xy for xy in self.items]), "},"
        # da(self.portal)
        print >>hh, "%d," % self.air
        print >>hh, "%d," % self.conveyordir
        # print >>hh, "%d," % self.portalattr
        print >>hh, "%d,%d," % self.portalxy
        # print >>hh, "{", ",".join([init(cc) for cc in self.guardian]), "},"
        print >>hh, "{", ",".join(["{%d,%d,%d,%d,%d,%d}" % xx for xx in self.hguardians]), "},"
        print >>hh, "%d,%d," % self.willyxy
        print >>hh, "%d," % self.willyd
        print >>hh, "%d," % self.willyf
        print >>hh, "%d," % self.bidir
        print >>hh, "},"

class ManicMiner(gd2.prep.AssetBin):

    header = "../manicminer/manicminer.h"

    def addall(self):
        m = readz80("mm2.z80")
        screens = m[45056 - 16384:]
        levels = [Level(i, screens[1024*i:1024*(i+1)]) for i in range(19)]

        self.m = m
        self.screens = screens
        self.levels = levels

        assets = []
        assets.append(("maps", "".join([l.background_str() for l in levels])))

        willy = Image.fromstring("1", (16, 8 * 16), m[33280-16384:33536-16384].tostring())
        self.load_handle("WILLY", gd2.prep.split(16, 16, willy), gd2.L1)

        guardians = sum([l.guardian_images() for l in levels], [])
        self.load_handle("MANICMINER_ASSET_GUARDIANS", guardians, gd2.L1)

        tiles = sum([l.background_tiles() for l in levels], [])
        self.load_handle("MANICMINER_ASSET_TILES", tiles, gd2.RGB332)

        portals = [l.portal_image() for l in levels]
        self.load_handle("MANICMINER_ASSET_PORTALS", portals, gd2.RGB332)

        items = [l.item_image() for l in levels]
        self.load_handle("MANICMINER_ASSET_ITEMS", items, gd2.L1)

        self.load_handle("MANICMINER_ASSET_TITLE", [Image.open("mmtitle.png")], gd2.RGB332)

        eugene = levels[4].special_image()
        plinth = levels[1].special_image()
        boot = levels[2].special_image()
        self.load_handle("MANICMINER_ASSET_SPECIALS", [eugene, plinth, boot], gd2.L1)

        for (name, s) in assets:
            self.add("MANICMINER_ASSET_%s" % name.upper(), s)
        
    def extras(self, hh):

        m = self.m
        screens = self.screens
        levels = self.levels
        tune = m[34188-16384:34252-16384]
        def spec2midi(n):
            f = (128 * 261.63) / n
            midi = round(12 * math.log(f / 440, 2) + 69)
            return int(midi)
        tune = array.array('B', [spec2midi(n) for n in tune])

        print >>hh, "static const PROGMEM prog_uchar manicminer_tune[] = {"
        print >>hh, ",".join(["%d" % c for c in tune])
        print >>hh, "};";

        for lvl in levels:
            lvl.bidir = lvl.id in [0,1,2,3,4,5,6,9,15]

        print >>hh, "static const PROGMEM level levels[] = {"
        for lvl in levels:
            lvl.dump(hh)
            time.sleep(0)
        print >>hh, "};"

if __name__ == '__main__':
    mm = ManicMiner()
    mm.make()
