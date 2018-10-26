void MY_PROGRAM_init__(MY_PROGRAM *data__, BOOL retain) {
  __INIT_LOCATED(BOOL,__IX0_0,data__->BUTTON,retain)
  __INIT_LOCATED_VALUE(data__->BUTTON,__BOOL_LITERAL(FALSE))
  __INIT_LOCATED(BOOL,__QX0_0,data__->LAMP,retain)
  __INIT_LOCATED_VALUE(data__->LAMP,__BOOL_LITERAL(FALSE))
  TOF_init__(&data__->T0,retain);
}

// Code part
void MY_PROGRAM_body__(MY_PROGRAM *data__) {
  // Initialise TEMP variables

  __SET_VAR(data__->T0.,IN,,__GET_LOCATED(data__->BUTTON,));
  __SET_VAR(data__->T0.,PT,,__time_to_timespec(1, 2000, 0, 0, 0, 0));
  TOF_body__(&data__->T0);
  __SET_LOCATED(data__->,LAMP,,__GET_VAR(data__->T0.Q,));

  goto __end;

__end:
  return;
} // MY_PROGRAM_body__() 





