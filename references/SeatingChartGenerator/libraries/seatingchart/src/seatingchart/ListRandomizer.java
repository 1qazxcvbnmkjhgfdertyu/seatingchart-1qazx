package seatingchart;

import java.util.*;

public class ListRandomizer<E> {
  public List<E> randomize(List<E> list) {
    List<E> oldList = new ArrayList<E>(list);
    list = new ArrayList<E>(list);
    int listSize = list.size();
    
    List<E> newList = new ArrayList<E>(list.size());
    while(!list.isEmpty()) {
      int i = (int)Math.floor(Math.random() * list.size());
      newList.add(list.get(i));
      list.remove(i);
    }
    
    if(newList.size() != listSize)
      System.out.println("ERROR: Randomized list size (" + newList.size() + ") doesn't match original list size (" + listSize + ")" );
    
    if(oldList.equals(newList))
      System.out.println("WARNING: Randomized list and original list are equal");
    
    return newList;
  }
}
