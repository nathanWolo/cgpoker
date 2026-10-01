package org.slf4j;

/** No-op stand-in for slf4j's LoggerFactory (the referee logs through slf4j; the replayer discards it). */
public final class LoggerFactory {
  private static final Logger NOP = new Logger() {};
  private LoggerFactory() {}
  public static Logger getLogger(Class<?> c) { return NOP; }
  public static Logger getLogger(String name) { return NOP; }
}
