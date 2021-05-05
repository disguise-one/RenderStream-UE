Requires a camerareg d3 install (or d3 install featuring renderstream.dll) 

Usage as package:

*  create a Plugins folder in your project root directory if you haven't already.
*  copy the DisguiseUERenderStream package folder to the Plugins directory
*  after opening your project:
    *  go to edit > project settings > engine > rendering > default settings (expand) > frame buffer pixel format, and ensure it is 8bit RGBA (Restart will be required, see next step before restarting if alpha is desired)
	*  ONLY NEEDED FOR ALPHA: under rendering > post process, set "enable alpha channel support in processing" to "allow through tonemapper" (this will force a rebuild of all shaders and will be a lengthy startup time)
*  included in the plugin content folder are two template maps (one with a camera and one with a cine camera) and a RenderStreamMediaOutput uasset you can use these as a start point from here

If you've already made a non-trivial level (if you are already familiar with blueprints you just need to copy the blueprint logic from the template map to your own):

*  open one of the template maps and go to blueprints > open level blueprint 
*  click in the event graph and press ctrl-a to select all, copy this for pasting in your own level event graph
*  make a note of the 3 variables in the My Blueprint > Variables panel on the left, you will need to make these 3 in your own blueprint
*  go to your blueprint and paste into your event graph
*  in the My Blueprint > Variables panel start adding your variables (plus button), fill out the details via the Details > Variables panel on the right, the variables are as follows (13/06/2019):
    *  Vairable Name: RenderStreamOut		| Vairable Type: RenderStreamMediaOutput	| Instance Editable: Tick
	*  Vairable Name: RenderStreamCapture 	| Vairable Type: RenderStreamMediaCapture	| Instance Editable: UnTicked
	*  Vairable Name: SelectedCamera		| Vairable Type: Camera Actor				| Instance Editable: Tick
*  Press the compile button
*  RenderStreamOut and SelectedCamera must be set to some reference from the project via the Default Value panel below the Variable panel (Remember to compile and save after setting the default values)
*  RenderStreamOut can be set to the included RenderStreamMediaOutput asset or to a new one you create
*  SelectedCamera should be set to the camera actor (or cine camera actor) in your map you want to receive from, if you haven't made one do that now and come back and set this

The level blueprints in the template start capture immediately when play has started and stops on ending play, if you are comfortable modifying blueprints feel free to modify your level to capture under your specified circumstances


Known limitations

*  single camera capture only