Student findStudent(String fullName, List<Student> studentList) {
  fullName = fullName.toLowerCase().trim();
  for(Student s : studentList) {
    String sName = s.getName(NameStyle.FULL);
    sName = sName.toLowerCase().trim();
    if(sName.equals(fullName))
      return s;
  }
  
  return null;
}

String getFileExtension(String s) {
  String extension = s.substring(s.lastIndexOf('.') + 1);
  println(extension);
  return extension;
}
