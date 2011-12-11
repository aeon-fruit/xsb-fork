/* File:      table_inspection_defs.h
** Contact:   xsb-contact@cs.sunysb.edu
** 
** Copyright (C) The Research Foundation of SUNY, 1986, 1993-1998
** Copyright (C) ECRC, Germany, 1990
** 
** XSB is free software; you can redistribute it and/or modify it under the
** terms of the GNU Library General Public License as published by the Free
** Software Foundation; either version 2 of the License, or (at your option)
** any later version.
** 
** XSB is distributed in the hope that it will be useful, but WITHOUT ANY
** WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE.  See the GNU Library General Public License
** for more details.
** 
** You should have received a copy of the GNU Library General Public License
** along with XSB; if not, write to the Free Software Foundation,
** Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** 
*/

#define FIND_COMPONENTS                          0
#define FIND_FORWARD_DEPENDENCIES                1
#define FIND_BACKWARD_DEPENDENCIES               2
#define FIND_ANSWERS                             3
#define CALL_SUBS_SLG_NOT                        4
#define GET_PRED_CALLTRIE_PTR                    5
#define GET_CALLSTO_NUMBER                       6
#define GET_ANSWER_NUMBER                        7
#define EARLY_COMPLETE_ON_NTH                    8
#define EARLY_COMPLETE                           9
#define START_FOREST_VIEW                        10
#define STOP_FOREST_VIEW                         11
#define TNOT_SETUP                               12
#define GET_CURRENT_SCC                          13
#define PRINT_COMPLETION_STACK                   14

// For delete return
#define ANSWER_SUBSUMPTION   0
#define USER_DELETE          1
