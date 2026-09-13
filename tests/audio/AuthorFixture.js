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
