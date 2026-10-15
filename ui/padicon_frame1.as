stop();
// The UI is drawn with exactFit (1280x720 stage stretched to the screen), so
// on non-16:9 screens the round button glyph gets squashed; undo that around its centre.
var stretch = 1;
if(Stage.scaleMode == "exactFit" && System.capabilities.screenResolutionX > 0 && System.capabilities.screenResolutionY > 0)
{
   stretch = System.capabilities.screenResolutionY / Stage.height / (System.capabilities.screenResolutionX / Stage.width);
}
if(stretch != 1)
{
   var b = glyph_mc.getBounds(glyph_mc);
   var cx = (b.xMin + b.xMax) / 2;
   glyph_mc._xscale = 100 * stretch;
   glyph_mc._x = cx - cx * stretch;
}
