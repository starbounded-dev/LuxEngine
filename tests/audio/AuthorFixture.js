var events=studio.project.model.Event.findInstances();
var names=['Speed','Weight','Surface','Impulse'];
var presets=[];
for(var i=0;i<events.length;i++) {
 var e=events[i];
 for(var j=0;j<names.length;j++) {
  var p=e.addGameParameter(i==0 ? {name:names[j],type:studio.project.parameterType.User,min:0,max:10000} : presets[j]);
  if(i==0) presets[j]=p.preset;
  var curve=e.mixer.masterBus.addAutomator('volume').addAutomationCurve(p.preset);
  curve.addAutomationPoint(0,0);curve.addAutomationPoint(10000,0);
 }
}
studio.project.save();

var musicPresets=[];
var firstMusic=true;
for(var i=0;i<events.length;i++) {
 var e=events[i];
 if(e.name!='MusicBed' && e.name!='MusicOther') continue;
 e.timeline.isProxyEnabled=true;
 var defs=[
  {name:'State',type:studio.project.parameterType.UserEnumeration,min:0,max:1,enumerationLabels:['Explore','Combat']},
  {name:'Intensity',type:studio.project.parameterType.User,min:0,max:1},
  {name:'Layer_Drums',type:studio.project.parameterType.User,min:0,max:1}
 ];
 for(var j=0;j<defs.length;j++) {
  var p=e.addGameParameter(firstMusic ? defs[j] : musicPresets[j]);
  if(firstMusic) musicPresets[j]=p.preset;
  var curve=e.mixer.masterBus.addAutomator('volume').addAutomationCurve(p.preset);
  curve.addAutomationPoint(0,0);curve.addAutomationPoint(1,0);
 }
 firstMusic=false;
 var track=e.markerTracks[0];
 track.addNamedMarker('Cue',0.25);
 track.addNamedMarker('Section:Verse',0.5);
 track.addRegion(0,2,'Loop',studio.project.regionLoopMode.Looping);
 var tempo=studio.project.create('TempoMarker');
 tempo.position=0;
 tempo.tempo=240;
 tempo.markerTrack=track;
 tempo.timeline=e.timeline;
}
studio.project.save();
