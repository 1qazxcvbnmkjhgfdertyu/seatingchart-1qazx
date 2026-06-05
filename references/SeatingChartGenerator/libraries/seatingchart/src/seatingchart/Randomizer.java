package seatingchart;

import java.util.List;

public interface Randomizer {
  public SeatingArrangement randomize(SeatingArrangement arrangement, List<Student> students, boolean randomizeDesks, boolean reverseDesks);
}
