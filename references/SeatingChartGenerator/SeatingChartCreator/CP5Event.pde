List<CP5Event> eventActions;

void setupEvents() {
  eventActions = new ArrayList<CP5Event>();
}

void addEvent(Object o, CP5EventAction a) {
  eventActions.add(new CP5Event(o, a));
}

void controlEvent(ControlEvent event) {
  println("ControlEvent: " + event.toString());
  for(CP5Event e : eventActions) {
    e.runIfMatch(event);
  }
}
