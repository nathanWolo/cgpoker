package org.slf4j;

/** No-op stand-in for the slf4j-api Logger: just the calls the referee model classes make. */
public interface Logger {
  default void debug(String msg, Object... args) {}
  default void info(String msg, Object... args) {}
  default void warn(String msg, Object... args) {}
  default void error(String msg, Object... args) {}
}
