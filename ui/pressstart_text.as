// Show the Xbox A button (mapped to Enter) in place of the word ENTER.
// The field is translated after this frame runs, so wait for the text,
// then swap the word for spaces as wide as the icon and put the icon there.
// Exact char boundaries are ink bounds (empty for spaces), so measure the
// gap between the visible characters around the word. They also ignore the
// centre alignment of the line, so add that offset when placing the icon.
_global.gfxExtensions = true;
var waitFrames = 0;
this.onEnterFrame = function()
{
   waitFrames++;
   var at = press_txt.text.indexOf("ENTER");
   if(at >= 0)
   {
      delete this.onEnterFrame;
      var before = at - 1;
      while(before > 0 && press_txt.text.charAt(before) == " ")
      {
         before--;
      }
      var glyph = this.attachMovie("pad_A_mc","padA_mc",201);
      // exactFit stretches the 1280x720 stage to the screen; keep the icon round.
      if(Stage.scaleMode == "exactFit" && System.capabilities.screenResolutionX > 0 && System.capabilities.screenResolutionY > 0)
      {
         glyph._xscale = 100 * (System.capabilities.screenResolutionY / Stage.height) / (System.capabilities.screenResolutionX / Stage.width);
      }
      var gb = glyph.getBounds(this);
      var glyphWidth = gb.xMax - gb.xMin;
      var count = 1;
      press_txt.replaceText(at,at + 5," ");
      var left = press_txt.getExactCharBoundaries(before);
      var right = press_txt.getExactCharBoundaries(at + count);
      while(count < 12 && right.x - (left.x + left.width) < glyphWidth + 8)
      {
         press_txt.replaceText(at,at," ");
         count++;
         left = press_txt.getExactCharBoundaries(before);
         right = press_txt.getExactCharBoundaries(at + count);
      }
      // Keep the icon close to what follows the word (", ..."), like the word was.
      var alignOffset = (press_txt._width - 4 - press_txt.textWidth) / 2;
      var centerY = left.y + left.height / 2;
      glyph._x = press_txt._x + alignOffset + right.x - 3 - gb.xMax;
      glyph._y = press_txt._y + centerY - (gb.yMin + gb.yMax) / 2;
   }
   else if(waitFrames > 300)
   {
      delete this.onEnterFrame;
   }
};
