//=========================================================
//  MusE
//  Linux Music Editor
//  $Id: undo.cpp,v 1.12.2.9 2009/05/24 21:43:44 terminator356 Exp $
//
//  (C) Copyright 1999/2000 Werner Schweer (ws@seh.de)
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; version 2 of
//  the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
//=========================================================

#include "assert.h"

#include "sig.h"  
#include "keyevent.h"

#include "undo.h"
#include "song.h"
#include "globals.h"
#include "audio.h"  
#include "midiport.h"
#include "operations.h"
#include "tempo.h"
#include "audiodev.h"
#include "wave_helper.h"
#include "gconfig.h"
#include "al/al.h"

//#include <string.h>
//#include <QAction>
#include <set>

// Forwards from header:
#include "track.h"
#include "part.h"
#include "ctrl.h"

// Enable for debugging:
//#define _UNDO_DEBUG_

namespace MusECore {

// iundo points to last Undo() in Undo-list

static bool undoMode = false;  // for debugging

std::list<QString> temporaryWavFiles;

//---------------------------------------------------------
//   typeName
//---------------------------------------------------------

const char* UndoOp::typeName()
      {
      static const char* name[] = {
            "AddRoute", "DeleteRoute", 
            "AddTrack", "DeleteTrack", 
            "AddPart",  "DeletePart", "MovePart", "ModifyPartStart", "ModifyPartLength", "ModifyPartName", "SelectPart",
            "AddEvent", "DeleteEvent", "ModifyEvent", "SelectEvent",
            "AddAudioCtrlVal", "DeleteAudioCtrlVal", "ModifyAudioCtrlVal", "ModifyAudioCtrlValList",
            "AddTempo", "DeleteTempo", "ModifyTempo", "SetTempo", "SetStaticTempo", "SetGlobalTempo", "EnableMasterTrack",
            "AddSig",   "DeleteSig",   "ModifySig",
            "AddKey",   "DeleteKey",   "ModifyKey",
            "ModifyTrackName", "ModifyTrackChannel",
            "SetTrackRecord", "SetTrackMute", "SetTrackSolo", "SetTrackRecMonitor", "SetTrackOff",
            "MoveTrack",
            "ModifyClip", "AddMarker", "DeleteMarker", "ModifyMarker", "SetMarkerPos",
            "ModifySongLen", "SetInstrument", "DoNothing",
            "ModifyMidiDivision",
            "EnableAllAudioControllers",
            "GlobalSelectAllEvents",
            "NormalizeMidiDivision"
            };
      return name[type];
      }

//---------------------------------------------------------
//   dump
//---------------------------------------------------------

void UndoOp::dump()
      {
      printf("UndoOp: %s\n   ", typeName());
      switch(type) {
            case AddTrack:
            case DeleteTrack:
                  printf("%d %s\n", trackno, track->name().toLatin1().constData());
                  break;
            case AddEvent:
            case DeleteEvent:
                  printf("old event:\n");
                  oEvent.dump(5);
                  printf("   new event:\n");
                  nEvent.dump(5);
                  printf("   Part:\n");
                  if (part)
                        part->dump(5);
                  break;
            case ModifyTrackName:
                  printf("<%s>-<%s>\n", _oldName->toLocal8Bit().data(), _newName->toLocal8Bit().data());
                  break;
            case ModifyTrackChannel:
                  printf("%s <%d>-<%d>\n", track->name().toLatin1().constData(), _oldPropValue, _newPropValue);
                  break;
            case SetTrackRecord:
                  printf("%s %d\n", track->name().toLatin1().constData(), a);
                  break;
            case SetTrackMute:
                  printf("%s %d\n", track->name().toLatin1().constData(), a);
                  break;
            case SetTrackSolo:
                  printf("%s %d\n", track->name().toLatin1().constData(), a);
                  break;
            case SetTrackRecMonitor:
                  printf("%s %d\n", track->name().toLatin1().constData(), a);
                  break;
            case SetTrackOff:
                  printf("%s %d\n", track->name().toLatin1().constData(), a);
                  break;
            default:      
                  break;
            }
      }

//---------------------------------------------------------
//    clearDelete
//---------------------------------------------------------

void UndoList::clearDelete()
{
  if(!empty())
  {
    if (this->isUndo)
    {
      for(iUndo iu = begin(); iu != end(); ++iu)
      {
        Undo& u = *iu;
        for(iUndoOp i = u.begin(); i != u.end(); ++i)
        {
          switch(i->type)
          {
            case UndoOp::DeleteTrack:
                  if(i->track)
                    delete const_cast<Track*>(i->track);
                  break;
                  
            case UndoOp::DeletePart:
                  delete const_cast<Part*>(i->part);
                  break;

            case UndoOp::ModifyMarker:
            case UndoOp::SetMarkerPos:
            case UndoOp::AddMarker:
            case UndoOp::DeleteMarker:
                  if (i->oldMarker)
                    delete i->oldMarker;
                  if (i->newMarker)
                    delete i->newMarker;
                  break;
                  
            case UndoOp::ModifyPartName:
            case UndoOp::ModifyTrackName:
                  if (i->_oldName)
                    delete i->_oldName;
                  if (i->_newName)
                    delete i->_newName;
                  break;
            
            case UndoOp::ModifyAudioCtrlValList:
                  if (i->_eraseCtrlList)
                    delete i->_eraseCtrlList;
                  if (i->_addCtrlList)
                    delete i->_addCtrlList;
                  break;
                  
            default:
                  break;
          }
        }
        u.clear();
      }
    }
    else
    {
      for(riUndo iu = rbegin(); iu != rend(); ++iu)
      {
        Undo& u = *iu;
        for(riUndoOp i = u.rbegin(); i != u.rend(); ++i)
        {
          switch(i->type)
          {
            case UndoOp::AddTrack:
                  delete i->track;
                  break;
                  
            case UndoOp::AddPart:
                  delete i->part;
                  break;

            case UndoOp::ModifyMarker:
            case UndoOp::SetMarkerPos:
            case UndoOp::AddMarker:
            case UndoOp::DeleteMarker:
                  if (i->oldMarker)
                    delete i->oldMarker;
                  if (i->newMarker)
                    delete i->newMarker;
                  break;
                  
            case UndoOp::ModifyPartName:
            case UndoOp::ModifyTrackName:
                  if (i->_oldName)
                    delete i->_oldName;
                  if (i->_newName)
                    delete i->_newName;
                  break;
            
            case UndoOp::ModifyAudioCtrlValList:
                  if (i->_eraseCtrlList)
                    delete i->_eraseCtrlList;
                  if (i->_addCtrlList)
                    delete i->_addCtrlList;
                  break;
                  
            default:
                  break;
          }
        }
        u.clear();
      }
    }
  }

  clear();
}

//---------------------------------------------------------
//    startUndo
//---------------------------------------------------------

void Song::startUndo(void* sender)
      {
      redoList->clearDelete(); // redo must be invalidated when a new undo is started
      MusEGlobal::redoAction->setEnabled(false);
      setUndoRedoText();
      
      undoList->push_back(Undo());
      updateFlags = SongChangedStruct_t(0, 0, sender);
      undoMode = true;
      }

//---------------------------------------------------------
//   endUndo
//---------------------------------------------------------

void Song::endUndo(SongChangedStruct_t flags)
      {
      // It is possible the current list may be empty after our optimizations during appending 
      //  of given operations to the current list. (Or if no operations were pushed between startUndo and endUndo).
      // Get rid of an empty current list now.
      if(undoList->back().empty())
        undoList->pop_back();
      else 
      {
        riUndo prev_undo = undoList->rbegin();
        prev_undo++;
        if (prev_undo!=undoList->rend())
        {
              // try to merge the current Undo with the last one
              if (prev_undo->merge_combo(undoList->back()))
                    undoList->pop_back();
        }
      }
      
      // Even if the current list was empty, or emptied during appending of given operations to the current list, 
      //  the given operations were executed so we still need to inform that something may have changed.
      
      updateFlags |= flags;
      endMsgCmd();
      undoMode = false;
      }

//---------------------------------------------------------
//   setUndoRedoText
//---------------------------------------------------------

void Song::setUndoRedoText()
{
  if(MusEGlobal::undoAction)
  {
    QString s = tr("Und&o");
    if(MusEGlobal::undoAction->isEnabled())
    {
      if(!undoList->empty() && !undoList->back().empty())
      {
        int sz = undoList->back().size();
        //if(sz >= 2)
        //  s += QString(" (%1)").arg(sz);
        s += QString(" ") + undoList->back().front().typeName();
        if(sz >= 2)
          s += ", ..";  // Hm, the tooltip will not show three dots "..."
      }
    }
    MusEGlobal::undoAction->setText(s);
  }
  
  if(MusEGlobal::redoAction)
  {
    QString s = tr("Re&do");
    if(MusEGlobal::redoAction->isEnabled())
    {
      if(!redoList->empty() && !redoList->back().empty())
      {
        int sz = redoList->back().size();
        //if(sz >= 2)
        //  s += QString(" (%1)").arg(sz);
        s += QString(" ") + redoList->back().front().typeName();
        if(sz >= 2)
          s += ", ..";
      }
    }
    MusEGlobal::redoAction->setText(s);
  }
}

void Undo::push_front(const UndoOp& op)
{
  insert(begin(), op);
}

void Undo::push_back(const UndoOp& op)
{
  insert(end(), op);
}

void Undo::insert(Undo::iterator position, Undo::const_iterator first, Undo::const_iterator last)
{
  for(Undo::const_iterator iuo = first; iuo != last; ++iuo)
    insert(position, *iuo);
}

void Undo::insert(Undo::iterator position, Undo::size_type n, const UndoOp& op)
{
  for(Undo::size_type i = 0; i != n; ++i)
    insert(position, op);
}

void Undo::insert(Undo::iterator position, const UndoOp& op)
{
  UndoOp n_op = op;

#ifdef _UNDO_DEBUG_
  switch(n_op.type)
  {
    case UndoOp::AddRoute:
      fprintf(stderr, "Undo::insert: AddRoute\n");
    break;
    case UndoOp::DeleteRoute:
      fprintf(stderr, "Undo::insert: DeleteRoute\n");
    break;

    
    case UndoOp::AddTrack:
      fprintf(stderr, "Undo::insert: AddTrack\n");
    break;
    case UndoOp::DeleteTrack:
      fprintf(stderr, "Undo::insert: DeleteTrack\n");
    break;
    case UndoOp::MoveTrack:
      fprintf(stderr, "Undo::insert: MoveTrack\n");
    break;
    case UndoOp::ModifyTrackName:
      fprintf(stderr, "Undo::insert: ModifyTrackName\n");
    break;
    case UndoOp::ModifyTrackChannel:
      fprintf(stderr, "Undo::insert: ModifyTrackChannel\n");
    break;
    case UndoOp::SetTrackRecord:
      fprintf(stderr, "Undo::insert: SetTrackRecord\n");
    break;
    case UndoOp::SetTrackMute:
      fprintf(stderr, "Undo::insert: SetTrackMute\n");
    break;
    case UndoOp::SetTrackSolo:
      fprintf(stderr, "Undo::insert: SetTrackSolo\n");
    break;
    case UndoOp::SetTrackRecMonitor:
      fprintf(stderr, "Undo::insert: SetTrackRecMonitor\n");
    break;
    case UndoOp::SetTrackOff:
      fprintf(stderr, "Undo::insert: SetTrackOff\n");
    break;
    
    
    case UndoOp::AddPart:
      fprintf(stderr, "Undo::insert: AddPart\n");
    break;
    case UndoOp::DeletePart:
      fprintf(stderr, "Undo::insert: DeletePart\n");
    break;
    case UndoOp::MovePart:
      fprintf(stderr, "Undo::insert: MovePart\n");
    break;
    case UndoOp::SelectPart:
      fprintf(stderr, "Undo::insert: SelectPart\n");
    break;
    case UndoOp::ModifyPartName:
      fprintf(stderr, "Undo::insert: ModifyPartName\n");
    break;
    case UndoOp::ModifyPartStart:
      fprintf(stderr, "Undo::insert: ModifyPartStart\n");
    break;
    case UndoOp::ModifyPartLength:
      fprintf(stderr, "Undo::insert: ModifyPartLength\n");
    break;
    
    
    case UndoOp::AddEvent:
      fprintf(stderr, "Undo::insert: AddEvent\n");
    break;
    case UndoOp::DeleteEvent:
      fprintf(stderr, "Undo::insert: DeleteEvent\n");
    break;
    case UndoOp::ModifyEvent:
      fprintf(stderr, "Undo::insert: ModifyEvent\n");
    break;
    case UndoOp::SelectEvent:
      fprintf(stderr, "Undo::insert: SelectEvent\n");
    break;
    
    
    case UndoOp::AddAudioCtrlVal:
      fprintf(stderr, "Undo::insert: AddAudioCtrlVal\n");
    break;
    case UndoOp::DeleteAudioCtrlVal:
      fprintf(stderr, "Undo::insert: DeleteAudioCtrlVal\n");
    break;
    case UndoOp::ModifyAudioCtrlVal:
      fprintf(stderr, "Undo::insert: ModifyAudioCtrlVal\n");
    break;
    case UndoOp::ModifyAudioCtrlValList:
      fprintf(stderr, "Undo::insert: ModifyAudioCtrlValList\n");
    break;
    
    
    case UndoOp::AddTempo:
      fprintf(stderr, "Undo::insert: AddTempo tempo:%d tick:%d\n", n_op.b, n_op.a);
    break;
    case UndoOp::DeleteTempo:
      fprintf(stderr, "Undo::insert: DeleteTempo old val:%d tick:%d\n", n_op.b, n_op.a);
    break;
    case UndoOp::ModifyTempo:
      fprintf(stderr, "Undo::insert: ModifyTempo old:%d new:%d tick:%d\n", n_op.b, n_op.c, n_op.a);
    break;
    case UndoOp::SetTempo:
      fprintf(stderr, "Undo::insert: SetTempo tempo:%d tick:%d\n", n_op.b, n_op.a);
    break;
    case UndoOp::SetStaticTempo:
      fprintf(stderr, "Undo::insert: SetStaticTempo\n");
    break;
    case UndoOp::SetGlobalTempo:
      fprintf(stderr, "Undo::insert: SetGlobalTempo\n");
    break;
    case UndoOp::EnableMasterTrack:
      fprintf(stderr, "Undo::insert: EnableMasterTrack\n");
    break;
    
    
    case UndoOp::AddSig:
      fprintf(stderr, "Undo::insert: AddSig\n");
    break;
    case UndoOp::DeleteSig:
      fprintf(stderr, "Undo::insert: DeleteSig\n");
    break;
    case UndoOp::ModifySig:
      fprintf(stderr, "Undo::insert: ModifySig\n");
    break;
    
    
    case UndoOp::AddKey:
      fprintf(stderr, "Undo::insert: AddKey\n");
    break;
    case UndoOp::DeleteKey:
      fprintf(stderr, "Undo::insert: DeleteKey\n");
    break;
    case UndoOp::ModifyKey:
      fprintf(stderr, "Undo::insert: ModifyKey\n");
    break;
    
    
    case UndoOp::ModifyClip:
      fprintf(stderr, "Undo::insert: ModifyClip\n");
    break;
    
    
    case UndoOp::AddMarker:
      fprintf(stderr, "Undo::insert: AddMarker\n");
    break;

    case UndoOp::DeleteMarker:
      fprintf(stderr, "Undo::insert: DeleteMarker\n");
    break;

    case UndoOp::ModifyMarker:
      fprintf(stderr, "Undo::insert: ModifyMarker\n");
    break;

    case UndoOp::SetMarkerPos:
      fprintf(stderr, "Undo::insert: SetMarkerPos\n");
    break;

    
    case UndoOp::ModifySongLen:
      fprintf(stderr, "Undo::insert: ModifySongLen\n");
    break;

    case UndoOp::SetInstrument:
      fprintf(stderr, "Undo::insert: SetInstrument\n");
    break;

    
    case UndoOp::DoNothing:
      fprintf(stderr, "Undo::insert: DoNothing\n");
    break;
    
    case UndoOp::ModifyMidiDivision:
      fprintf(stderr, "Undo::insert: ModifyMidiDivision\n");
    break;
    
    case UndoOp::EnableAllAudioControllers:
      fprintf(stderr, "Undo::insert: EnableAllAudioControllers\n");
    break;
    
    case UndoOp::GlobalSelectAllEvents:
      fprintf(stderr, "Undo::insert: GlobalSelectAllEvents\n");
    break;
    
    case UndoOp::NormalizeMidiDivision:
      fprintf(stderr, "Undo::insert: NormalizeMidiDivision\n");
    break;
    
    default:
    break;
  }
#endif

  // (NOTE: Use this handy speed-up 'if' line to exclude unhandled operation types)
  if(n_op.type != UndoOp::ModifyTrackChannel && n_op.type != UndoOp::ModifyClip && n_op.type != UndoOp::DoNothing) 
  {
    // TODO FIXME: Must look beyond position and optimize in that direction too !
    //for(Undo::iterator iuo = begin(); iuo != position; ++iuo)
    iterator iuo = position;
    while(iuo != begin())
    {
      --iuo;
      UndoOp& uo = *iuo;
      
      switch(n_op.type)
      {
        case UndoOp::AddRoute:
          if(uo.type == UndoOp::AddRoute && uo.routeFrom == n_op.routeFrom && uo.routeTo == n_op.routeTo)
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double AddRoute. Ignoring.\n");
            return;
          }
          else if(uo.type == UndoOp::DeleteRoute && uo.routeFrom == n_op.routeFrom && uo.routeTo == n_op.routeTo)
          {
            // Delete followed by add is useless. Cancel out the delete + add by erasing the delete command.
            erase(iuo);
            return;  
          }
        break;
        
        case UndoOp::DeleteRoute:
          if(uo.type == UndoOp::DeleteRoute && uo.routeFrom == n_op.routeFrom && uo.routeTo == n_op.routeTo)  
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double DeleteRoute. Ignoring.\n");
            return;  
          }
          else if(uo.type == UndoOp::AddRoute && uo.routeFrom == n_op.routeFrom && uo.routeTo == n_op.routeTo)  
          {
            // Add followed by delete is useless. Cancel out the add + delete by erasing the add command.
            erase(iuo);
            return;  
          }
        break;

        
        case UndoOp::ModifyTrackName:
          if(uo.type == UndoOp::ModifyTrackName && uo.track == n_op.track)  
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double ModifyTrackName. Ignoring.\n");
            return;  
          }
        break;
        
        case UndoOp::MoveTrack:
          if(uo.type == UndoOp::MoveTrack && uo.a == n_op.a)
          {
            // Simply replace the 'to track' value.
            uo.b = n_op.b;
            return;
          }
        break;
        
        case UndoOp::SetTrackRecord:
          if(uo.type == UndoOp::SetTrackRecord && uo.track == n_op.track)
          {
            if(uo.a == n_op.a)
            {
              fprintf(stderr, "MusE error: Undo::insert(): Double SetTrackRecord. Ignoring.\n");
              return;
            }
            else
            {
              // On/off followed by off/on is useless. Cancel out the on/off + off/on by erasing the command.
              erase(iuo);
              return;  
            }
          }
        break;
        
        case UndoOp::SetTrackMute:
          if(uo.type == UndoOp::SetTrackMute && uo.track == n_op.track)
          {
            if(uo.a == n_op.a)
            {
              fprintf(stderr, "MusE error: Undo::insert(): Double SetTrackMute. Ignoring.\n");
              return;
            }
            else
            {
              // On/off followed by off/on is useless. Cancel out the on/off + off/on by erasing the command.
              erase(iuo);
              return;  
            }
          }
        break;
        
        case UndoOp::SetTrackSolo:
          if(uo.type == UndoOp::SetTrackSolo && uo.track == n_op.track)
          {
            if(uo.a == n_op.a)
            {
              fprintf(stderr, "MusE error: Undo::insert(): Double SetTrackSolo. Ignoring.\n");
              return;
            }
            else
            {
              // On/off followed by off/on is useless. Cancel out the on/off + off/on by erasing the command.
              erase(iuo);
              return;  
            }
          }
        break;
        
        case UndoOp::SetTrackRecMonitor:
          if(uo.type == UndoOp::SetTrackRecMonitor && uo.track == n_op.track)
          {
            if(uo.a == n_op.a)
            {
              fprintf(stderr, "MusE error: Undo::insert(): Double SetTrackRecMonitor. Ignoring.\n");
              return;
            }
            else
            {
              // On/off followed by off/on is useless. Cancel out the on/off + off/on by erasing the command.
              erase(iuo);
              return;  
            }
          }
        break;
        
        case UndoOp::SetTrackOff:
          if(uo.type == UndoOp::SetTrackOff && uo.track == n_op.track)
          {
            if(uo.a == n_op.a)
            {
              fprintf(stderr, "MusE error: Undo::insert(): Double SetTrackOff. Ignoring.\n");
              return;
            }
            else
            {
              // On/off followed by off/on is useless. Cancel out the on/off + off/on by erasing the command.
              erase(iuo);
              return;  
            }
          }
        break;
        
        case UndoOp::ModifyPartName:
          if(uo.type == UndoOp::ModifyPartName && uo.part == n_op.part)  
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double ModifyPartName. Ignoring.\n");
            return;  
          }
        break;

      case UndoOp::ModifyPartStart:
          // TODO: events_offset is a difference requiring accumulation not simple replacement,
          //        and events_offset_time_type might be different requiring conversion. 
//           if(uo.type == UndoOp::ModifyPartStart)
//           {
//             if(uo.part == n_op.part)
//             {
//               // Simply replace the new values.
//               uo.new_partlen_or_pos = n_op.new_partlen_or_pos;
//               uo.new_partlen = n_op.new_partlen;
//               uo.events_offset = n_op.events_offset;
//               uo.events_offset_time_type = n_op.events_offset_time_type;
//               return;
//             }
//           }
          break;

      case UndoOp::ModifyPartLength:
          // TODO: events_offset is a difference requiring accumulation not simple replacement,
          //        and events_offset_time_type might be different requiring conversion. 
//           if(uo.type == UndoOp::ModifyPartLength)
//           {
//             if(uo.part == n_op.part)
//             {
//               // Simply replace the new values.
//               uo.new_partlen_or_pos = n_op.new_partlen_or_pos;
//               uo.events_offset = n_op.events_offset;
//               uo.events_offset_time_type = n_op.events_offset_time_type;
//               return;
//             }
//           }
        break;
        
        case UndoOp::MovePart:
          if(uo.type == UndoOp::MovePart && uo.part == n_op.part)
          {
            // Simply replace the new value and new track.
            uo.new_partlen_or_pos = n_op.new_partlen_or_pos;
            uo.track = n_op.track;
            return;
          }
        break;
        
        case UndoOp::AddPart:
          if(uo.type == UndoOp::AddPart && uo.part == n_op.part)  
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double AddPart. Ignoring.\n");
            return;  
          }
          else if(uo.type == UndoOp::DeletePart && uo.part == n_op.part)  
          {
            // Delete followed by add is useless. Cancel out the delete + add by erasing the delete command.
            erase(iuo);
            return;  
          }
        break;
        
        case UndoOp::DeletePart:
          if(uo.type == UndoOp::DeletePart && uo.part == n_op.part)  
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double DeletePart. Ignoring.\n");
            return;  
          }
          else if(uo.type == UndoOp::AddPart && uo.part == n_op.part)  
          {
            // Add followed by delete is useless. Cancel out the add + delete by erasing the add command.
            erase(iuo);
            return;  
          }
        break;

        
        case UndoOp::AddEvent:
          if(uo.type == UndoOp::AddEvent && uo.nEvent == n_op.nEvent && uo.part == n_op.part)  
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double AddEvent. Ignoring.\n");
            return;  
          }
          else if(uo.type == UndoOp::DeleteEvent && uo.part == n_op.part)  
          {
            if(uo.nEvent == n_op.nEvent)
            {
              // Delete followed by add is useless. Cancel out the delete + add by erasing the delete command.
              erase(iuo);
              return;
            }
            else

            // To allow for easy DeleteEvent + AddEvent of a given controller number at a given time,
            //  instead of demanding ModifyEvent. Automatically transform the operations.
            if(uo.nEvent.type() == Controller && n_op.nEvent.type() == Controller &&
               uo.nEvent.dataA() == n_op.nEvent.dataA() &&
               uo.nEvent.posValue() == n_op.nEvent.posValue())
            {
              // Transform the DeleteEvent operation into a ModifyEvent operation.
              uo.type = UndoOp::ModifyEvent;
              uo.oEvent = uo.nEvent;
              uo.nEvent = n_op.nEvent;
              return;  
            }
          }
          else if(uo.type == UndoOp::ModifyEvent && uo.part == n_op.part)
          {
            if(uo.nEvent == n_op.nEvent)  
            {
              // Modify followed by adding of the modify's new event, is equivalent to just modifying with the added event. 
              fprintf(stderr, "MusE error: Undo::insert(): ModifyEvent, then AddEvent same new event (double AddEvent). Ignoring.\n");
              return;  
            }
            else if(uo.oEvent == n_op.nEvent)  
            {
              // Modify followed by adding of the modify's old event, is equivalent to just adding the event. 
              // Transform the ModifyEvent operation into an AddEvent.
              uo.type = UndoOp::AddEvent;
              uo.nEvent = uo.oEvent;
              return;  
            }
          }
        break;
        
        case UndoOp::DeleteEvent:
          if(uo.type == UndoOp::DeleteEvent && uo.nEvent == n_op.nEvent && uo.part == n_op.part)  
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double DeleteEvent. Ignoring.\n");
            return;  
          }
          else if(uo.type == UndoOp::AddEvent && uo.nEvent == n_op.nEvent && uo.part == n_op.part)  
          {
            // Add followed by delete is useless. Cancel out the add + delete by erasing the add command.
            erase(iuo);
            return;  
          }
          else if(uo.type == UndoOp::ModifyEvent && uo.part == n_op.part)  
          {
            if(uo.oEvent == n_op.nEvent)  
            {
              // Modify followed by delete of the modify's old event, is an error - two deletes of the same event. 
              fprintf(stderr, "MusE error: Undo::insert(): ModifyEvent, then DeleteEvent same old event (double DeleteEvent). Ignoring.\n");
              return;  
            }
            else if(uo.nEvent == n_op.nEvent)
            {
              // Modify followed by delete of the modify's new event, is equivalent to just deleting the old event. 
              // Transform the operation into a DeleteEvent.
              uo.type = UndoOp::DeleteEvent;
              uo.nEvent = uo.oEvent;
              return;  
            }
          }
        break;

        case UndoOp::ModifyEvent:
          if(n_op.oEvent == n_op.nEvent)
          {
            // Equivalent to deleting then adding the same event - useless, cancels out.
            return;
          }
          else if(uo.type == UndoOp::ModifyEvent && uo.part == n_op.part)  
          {
            // For testing...
            //fprintf(stderr, "MusE: DIAGNOSTIC: Undo::insert(): Double ModifyEvent... checking for errors...\n");
              
            if(uo.oEvent == n_op.oEvent)
            {
              if(uo.nEvent == n_op.nEvent)
              {
                fprintf(stderr, "MusE error: Undo::insert(): Double ModifyEvent. Ignoring.\n");
                return;
              }
              else
              {
                // For testing...
                //fprintf(stderr, "MusE: Undo::insert(): Double ModifyEvent. Same old events. Merging.\n");
                
                // Two modify commands with old events the same is equivalent to just one modify command.
                // Replace the existing ModifyEvent command's new event with the requested ModifyEvent command's new event.
                uo.nEvent = n_op.nEvent;
                return;  
              }
            }
            // REMOVE Tim. citem. Added. Remove. I think we CAN replace two different events with the same event.
            //else if(uo.nEvent == n_op.nEvent)
            //{
            //  // Cannot replace two different events with the same event.
            //  fprintf(stderr, "MusE error: Undo::insert(): Double ModifyEvent: different old events but same new event. Ignoring.\n");
            //  return;
            //}
            // Are inner new/old pair the same event?
            else if(uo.nEvent == n_op.oEvent) 
            {
              // Are outer old/new pair the same event?
              if(uo.oEvent == n_op.nEvent)
              {
                // First ModifyEvent old event and second ModifyEvent new event are both the same, equivalent to doing nothing.
                // Cancel out the two ModifyEvent operations by erasing the existing ModifyEvent command.
                erase(iuo);
                return;  
              }
              else
              {
                // For testing...
                //fprintf(stderr, "MusE: Undo::insert(): Double ModifyEvent. Inner new/old pair same, outer old/new pair not same. Merging to one ModifyEvent.\n");
            
                // Inner new/old pair are the same event and outer old/new pair are not the same event.
                // A modify command with new event followed by a modify command with old event the same
                //  is equivalent to just one modify command. Replace the existing ModifyEvent command's
                //  new event with the requested ModifyEvent command's new event.
                uo.nEvent = n_op.nEvent;
                return;  
              }
            }
            // Inner new/old pair are not the same event. Are outer old/new pair the same event?
            else if(uo.oEvent == n_op.nEvent) 
            {
                // For testing...
                //fprintf(stderr, "MusE: Undo::insert(): Double ModifyEvent. Inner new/old pair not same,"
                // " outer old/new pair same. Transforming to Add and Delete.\n");
            
              // Transform the existing ModifyEvent operation into an AddEvent.
              uo.type = UndoOp::AddEvent;
              // Transform the requested ModifyEvent operation into a DeleteEvent.
              n_op.type = UndoOp::DeleteEvent;
              n_op.nEvent = n_op.oEvent;
              // Allow it to add...
            }
          }
          else if(uo.type == UndoOp::AddEvent && uo.part == n_op.part)
          {
            // For testing...
            //fprintf(stderr, "MusE: Undo::insert(): AddEvent then ModifyEvent...\n");
            
            if(uo.nEvent == n_op.oEvent)
            {
              // For testing...
              //fprintf(stderr, "MusE: Undo::insert(): AddEvent then ModifyEvent. Same event. Merging to AddEvent.\n");
            
              // Add followed by modify with old event same as added event, is equivalent to just adding modify's new event.
              // Replace the existing AddEvent command's event with the requested ModifyEvent command's new event.
              uo.nEvent = n_op.nEvent;
              return;  
            }
            if(uo.nEvent == n_op.nEvent)
            {
              // Add followed by modify with new event same as added event, is a caller error.
              fprintf(stderr, "MusE error: Undo::insert(): AddEvent, then ModifyEvent same new event (double AddEvent). Ignoring.\n");
              return;  
            }
          }
          if(uo.type == UndoOp::DeleteEvent && uo.part == n_op.part)
          {
            if(uo.nEvent == n_op.oEvent)
            {
              // Delete followed by modify with old event same as deleted event, is an error.
              fprintf(stderr, "MusE error: Undo::insert(): DeleteEvent, then ModifyEvent same old event (double DeleteEvent). Ignoring.\n");
              return;  
            }
            if(uo.nEvent == n_op.nEvent)
            {
              // For testing...
              //fprintf(stderr, "MusE: Undo::insert(): DeleteEvent then ModifyEvent. Same event. Merging to DeleteEvent.\n");
            
              // Delete followed by modify with new event same as deleted event, is equivalent to just deleting modify's old event.
              // Replace the existing DeleteEvent command's event with the requested ModifyEvent command's old event.
              uo.nEvent = n_op.oEvent;
            }
          }
        break;

        case UndoOp::AddAudioCtrlVal:
          if(uo.type == UndoOp::AddAudioCtrlVal && uo.track == n_op.track &&
             uo._audioCtrlID == n_op._audioCtrlID &&
             uo._audioCtrlFrame == n_op._audioCtrlFrame)
          {
            // Simply replace the original value and frame.
            uo._audioCtrlVal = n_op._audioCtrlVal;
            return;  
          }
// TODO If possible.
//           else if(uo.type == UndoOp::DeleteAudioCtrlVal && uo.track == n_op.track &&
//              uo._audioCtrlID == n_op._audioCtrlID &&
//              uo._audioCtrlFrame == n_op._audioCtrlFrame)
//           {
//             // Delete followed by add, at the same frame. Transform the delete into a modify.
//             uo.type = UndoOp::ModifyAudioCtrlVal;
//             uo._audioCtrlVal = n_op._audioCtrlVal;
//             uo._audioNewCtrlFrame = 
//             return;  
//           }
        break;
        
        case UndoOp::DeleteAudioCtrlVal:
          if(uo.type == UndoOp::DeleteAudioCtrlVal && uo.track == n_op.track &&
             uo._audioCtrlID == n_op._audioCtrlID &&
             uo._audioCtrlFrame == n_op._audioCtrlFrame)
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double DeleteAudioCtrlVal. Ignoring.\n");
            return;  
          }
          else if(uo.type == UndoOp::AddAudioCtrlVal && uo.track == n_op.track &&
             uo._audioCtrlID == n_op._audioCtrlID &&
             uo._audioCtrlFrame == n_op._audioCtrlFrame)
          {
            // Add followed by delete, at the same frame, is useless. Cancel out the add + delete by erasing the add command.
            erase(iuo);
            return;  
          }
        break;

        case UndoOp::ModifyAudioCtrlVal:
          if(uo.type == UndoOp::ModifyAudioCtrlVal && uo.track == n_op.track &&
             uo._audioCtrlID == n_op._audioCtrlID &&
             uo._audioNewCtrlFrame == n_op._audioCtrlFrame)
          {
            // Simply replace the original new value and new frame.
            uo._audioNewCtrlVal = n_op._audioNewCtrlVal;
            uo._audioNewCtrlFrame = n_op._audioNewCtrlFrame;
            return;  
          }
        break;

        case UndoOp::ModifyAudioCtrlValList:
          // Check the sanity of the requested op.
          if(n_op._eraseCtrlList == n_op._addCtrlList)
          {
            fprintf(stderr, "MusE error: Undo::insert(): ModifyAudioCtrlValList: Erase and add lists are the same. Ignoring.\n");
            return;
          }
          
          if(uo.type == UndoOp::ModifyAudioCtrlValList)
          {
            if(uo._ctrlListList == n_op._ctrlListList)
            {
              if(uo._addCtrlList == n_op._addCtrlList && uo._eraseCtrlList == n_op._eraseCtrlList)
              {
                fprintf(stderr, "MusE error: Undo::insert(): Double ModifyAudioCtrlValList. Ignoring.\n");
                return;
              }
              else if(uo._addCtrlList == n_op._eraseCtrlList)
              {
                // Delete the existing ModifyAudioCtrlValList command's _addCtrlList and replace it
                //  with the requested ModifyAudioCtrlValList command's _addCtrlList.
                if(uo._addCtrlList)
                  delete uo._addCtrlList;
                uo._addCtrlList = n_op._addCtrlList;
                return;
              }
            }
            // Seems possible... remove? But maybe dangerous to have two undo ops pointing to the same lists - they will be self-deleted.
            else
            {
              if(uo._addCtrlList == n_op._addCtrlList)
              {
                fprintf(stderr, "MusE error: Undo::insert(): ModifyAudioCtrlValList: Attempting to add same list to different containers. Ignoring.\n");
                return;
              }
              else if(uo._eraseCtrlList == n_op._eraseCtrlList)
              {
                fprintf(stderr, "MusE error: Undo::insert(): ModifyAudioCtrlValList: Attempting to erase same list from different containers. Ignoring.\n");
                return;
              }
            }
          }
        break;

        
        case UndoOp::SetInstrument:
          // Check the sanity of the requested op.
          if(n_op._oldMidiInstrument == n_op._newMidiInstrument)
          {
            fprintf(stderr, "MusE error: Undo::insert(): SetInstrument: Old and new instruments are the same. Ignoring.\n");
            return;
          }
          
          if(uo.type == UndoOp::SetInstrument)
          {
            if(uo._midiPort == n_op._midiPort)
            {
              if(uo._oldMidiInstrument == n_op._oldMidiInstrument && uo._newMidiInstrument == n_op._newMidiInstrument)
              {
                fprintf(stderr, "MusE error: Undo::insert(): Double SetInstrument. Ignoring.\n");
                return;
              }
              else if(uo._newMidiInstrument == n_op._oldMidiInstrument)
              {
                // Replace the existing SetInstrument command's _newMidiInstrument
                //  with the requested ModifyAudioCtrlValList command's _newMidiInstrument.
                uo._newMidiInstrument = n_op._newMidiInstrument;
                return;
              }
            }
          }
        break;


        case UndoOp::AddTempo:
          if(uo.type == UndoOp::AddTempo && uo.a == n_op.a)  
          {
            // Simply replace the value. 
            uo.b = n_op.b;
            return;
          }
          else if(uo.type == UndoOp::DeleteTempo && uo.a == n_op.a)  
          {
            // Delete followed by add. Transform the existing DeleteTempo operation into a ModifyTempo.
            uo.type = UndoOp::ModifyTempo;
            // a is already the tick, b is already the existing value from DeleteTempo, c is the new value.
            uo.c = n_op.b;
            return;  
          }
          else if(uo.type == UndoOp::ModifyTempo && uo.a == n_op.a)  
          {
            // Modify followed by add. Simply replace the value.
            // a is already the tick, b is already the existing value from ModifyTempo, c is the new value.
            uo.c = n_op.b;
            return;
          }
//           else if(uo.type == UndoOp::SetTempo && uo.a == n_op.a)  
//           {
//             // Only if the master is on.
//             if(MusEGlobal::tempomap.masterFlag())
//             {
//               // Simply replace the value. 
//               uo.b = n_op.b;
//               return;
//             }
//           }
        break;
        
        case UndoOp::DeleteTempo:
          if(uo.type == UndoOp::DeleteTempo && uo.a == n_op.a)  
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double DeleteTempo. Ignoring.\n");
            return;  
          }
          else if(uo.type == UndoOp::AddTempo && uo.a == n_op.a)  
          {
            // Add followed by delete is useless. Cancel out the add + delete by erasing the add command.
            erase(iuo);
            return;  
          }
          else if(uo.type == UndoOp::ModifyTempo && uo.a == n_op.a)  
          {
            // Modify followed by delete. Equivalent to delete. Transform existing ModifyTempo operation into a DeleteTempo.
            uo.type = UndoOp::DeleteTempo;
            // a is already the tick, b is already the existing old value from ModifyTempo.
            return;  
          }
//           else if(uo.type == UndoOp::SetTempo && uo.a == n_op.a)  
//           {
//             // Only if the master is on.
//             if(MusEGlobal::tempomap.masterFlag())
//             {
//               // Add followed by delete is useless. Cancel out the add + delete by erasing the add command.
//               erase(iuo);
//               return;
//             }
//           }
        break;
        
        case UndoOp::ModifyTempo:
          if(uo.type == UndoOp::ModifyTempo && uo.a == n_op.a)  
          {
            // Simply replace c with the new value.
            uo.c = n_op.c;
            return;  
          }
          else if(uo.type == UndoOp::AddTempo && uo.a == n_op.a)  
          {
            // Add followed by modify. Simply replace the add value.
            uo.b = n_op.c;
            return;  
          }
          else if(uo.type == UndoOp::DeleteTempo && uo.a == n_op.a)  
          {
            // Delete followed by modify. Equivalent to modify. Transform existing DeleteTempo operation into a ModifyTempo.
            uo.type = UndoOp::ModifyTempo;
            // a is already the tick, b is already the existing value from DeleteTempo. c is the new value from ModifyTempo.
            uo.c = n_op.c;
            return;  
          }
//           else if(uo.type == UndoOp::SetTempo && uo.a == n_op.a)  
//           {
//             // Only if the master is on.
//             if(MusEGlobal::tempomap.masterFlag())
//             {
//               // Add followed by modify. Simply replace the add value.
//               uo.b = n_op.c;
//               return;
//             }
//           }
        break;
          
//         case UndoOp::SetTempo:
//           if(uo.type == UndoOp::SetTempo && uo.a == n_op.a)  
//           {
//             // Simply replace the value. 
//             uo.b = n_op.b;
//             return;  
//           }
//           else if(uo.type == UndoOp::AddTempo && uo.a == n_op.a)  
//           {
//             // Simply replace the value. 
//             uo.b = n_op.b;
//             return;
//           }
//           else if(uo.type == UndoOp::DeleteTempo && uo.a == n_op.a)  
//           {
//             // Only if the master is on.
//             if(MusEGlobal::tempomap.masterFlag())
//             {
//               // Delete followed by add. Transform the existing DeleteTempo operation into a ModifyTempo.
//               uo.type = UndoOp::ModifyTempo;
//               // a is already the tick, b is already the existing value from DeleteTempo, c is the new value.
//               uo.c = n_op.b;
//               return;
//             }
//           }
//           else if(uo.type == UndoOp::ModifyTempo && uo.a == n_op.a)  
//           {
//             // Only if the master is on.
//             if(MusEGlobal::tempomap.masterFlag())
//             {
//               // Modify followed by add. Simply replace the value.
//               // a is already the tick, b is already the existing value from ModifyTempo, c is the new value.
//               uo.c = n_op.b;
//               return;
//             }
//           }
//           else if(uo.type == UndoOp::SetStaticTempo && uo.a == n_op.a)  
//           {
//             // Only if the master is not on.
//             if(!MusEGlobal::tempomap.masterFlag())
//             {
//               // Simply replace the value. 
//               uo.b = n_op.b;
//               return;
//             }
//           }
//         break;
        
        case UndoOp::SetStaticTempo:
          if(uo.type == UndoOp::SetStaticTempo)
          {
            // Simply replace a with the new value.
            uo.a = n_op.a;
            return;  
          }
        break;

        case UndoOp::SetGlobalTempo:
          if(uo.type == UndoOp::SetGlobalTempo)  
          {
            // Simply replace a with the new value.
            uo.a = n_op.a;
            return;  
          }
        break;

        case UndoOp::EnableMasterTrack:
          if(uo.type == UndoOp::EnableMasterTrack)
          {
            if(uo.a == n_op.a)
            {
              fprintf(stderr, "MusE error: Undo::insert(): Double EnableMasterTrack. Ignoring.\n");
              return;  
            }
            else
            {
              // Toggling is useless. Cancel out the enable or disable + disable or enable by erasing the disable or enable command.
              erase(iuo);
              return;  
            }
          }
        break;

        
        case UndoOp::AddSig:
          if(uo.type == UndoOp::AddSig && uo.a == n_op.a)  
          {
            // Simply replace the value. 
            uo.b = n_op.b;
            uo.c = n_op.c;
            return;  
          }
          else if(uo.type == UndoOp::DeleteSig && uo.a == n_op.a)  
          {
            // Delete followed by add. Transform the existing DeleteSig operation into a ModifySig.
            uo.type = UndoOp::ModifySig;
            // a is already the tick, b + c is already the existing value from DeleteSig, d + e is the new value.
            uo.d = n_op.b;
            uo.e = n_op.c;
            return;  
          }
          else if(uo.type == UndoOp::ModifySig && uo.a == n_op.a)  
          {
            // Modify followed by add. Simply replace the value.
            // a is already the tick, b + c is already the existing value from ModifySig, d + e is the new value.
            uo.d = n_op.b;
            uo.e = n_op.c;
            return;  
          }
        break;
        
        case UndoOp::DeleteSig:
          if(uo.type == UndoOp::DeleteSig && uo.a == n_op.a)  
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double DeleteSig. Ignoring.\n");
            return;  
          }
          else if(uo.type == UndoOp::AddSig && uo.a == n_op.a)  
          {
            // Add followed by delete is useless. Cancel out the add + delete by erasing the add command.
            erase(iuo);
            return;  
          }
          else if(uo.type == UndoOp::ModifySig && uo.a == n_op.a)  
          {
            // Modify followed by delete. Equivalent to delete. Transform existing ModifySig operation into a DeleteSig.
            uo.type = UndoOp::DeleteSig;
            // a is already the tick, b + c is already the existing old value from ModifySig.
            return;  
          }
        break;
        
        case UndoOp::ModifySig:
          if(uo.type == UndoOp::ModifySig && uo.a == n_op.a)  
          {
            // Simply replace d + e with the new value.
            uo.d = n_op.d;
            uo.e = n_op.e;
            return;  
          }
          else if(uo.type == UndoOp::AddSig && uo.a == n_op.a)  
          {
            // Add followed by modify. Simply replace the add value.
            uo.b = n_op.d;
            uo.c = n_op.e;
            return;  
          }
          else if(uo.type == UndoOp::DeleteSig && uo.a == n_op.a)  
          {
            // Delete followed by modify. Equivalent to modify. Transform existing DeleteSig operation into a ModifySig.
            uo.type = UndoOp::ModifySig;
            // a is already the tick, b + c is already the existing value from DeleteSig. d + e is the new value from ModifySig.
            uo.d = n_op.d;
            uo.e = n_op.e;
            return;  
          }
        break;
          
        
        case UndoOp::AddKey:
          if(uo.type == UndoOp::AddKey && uo.a == n_op.a)  
          {
            // Simply replace the value. 
            uo.b = n_op.b;
            uo.c = n_op.c;
            return;  
          }
          else if(uo.type == UndoOp::DeleteKey && uo.a == n_op.a)  
          {
            // Delete followed by add. Transform the existing DeleteKey operation into a ModifyKey.
            uo.type = UndoOp::ModifyKey;
            // a is already the tick, b + c is already the existing value from DeleteKey, d + e is the new value.
            uo.d = n_op.b;
            uo.e = n_op.c;
            return;  
          }
          else if(uo.type == UndoOp::ModifyKey && uo.a == n_op.a)  
          {
            // Modify followed by add. Simply replace the value.
            // a is already the tick, b + c is already the existing value from ModifyKey, d + e is the new value.
            uo.d = n_op.b;
            uo.e = n_op.c;
            return;  
          }
        break;
        
        case UndoOp::DeleteKey:
          if(uo.type == UndoOp::DeleteKey && uo.a == n_op.a)  
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double DeleteKey. Ignoring.\n");
            return;  
          }
          else if(uo.type == UndoOp::AddKey && uo.a == n_op.a)  
          {
            // Add followed by delete is useless. Cancel out the add + delete by erasing the add command.
            erase(iuo);
            return;  
          }
          else if(uo.type == UndoOp::ModifyKey && uo.a == n_op.a)  
          {
            // Modify followed by delete. Equivalent to delete. Transform existing ModifyKey operation into a DeleteKey.
            uo.type = UndoOp::DeleteKey;
            // a is already the tick, b + c is already the existing old value from ModifyKey.
            return;  
          }
        break;
        
        case UndoOp::ModifyKey:
          if(uo.type == UndoOp::ModifyKey && uo.a == n_op.a)  
          {
            // Simply replace d + e with the new value.
            uo.d = n_op.d;
            uo.e = n_op.e;
            return;  
          }
          else if(uo.type == UndoOp::AddKey && uo.a == n_op.a)  
          {
            // Add followed by modify. Simply replace the add value.
            uo.b = n_op.d;
            uo.c = n_op.e;
            return;  
          }
          else if(uo.type == UndoOp::DeleteKey && uo.a == n_op.a)  
          {
            // Delete followed by modify. Equivalent to modify. Transform existing DeleteSig operation into a ModifySig.
            uo.type = UndoOp::ModifyKey;
            // a is already the tick, b + c is already the existing value from DeleteKey. d + e is the new value from ModifyKey.
            uo.d = n_op.d;
            uo.e = n_op.e;
            return;  
          }
        break;

        
        case UndoOp::ModifySongLen:
          if(uo.type == UndoOp::ModifySongLen)  
          {
            // Simply replace a with the new value.
            uo.a = n_op.a;
            return;  
          }
        break;
        
        case UndoOp::ModifyMidiDivision:
          if(uo.type == UndoOp::ModifyMidiDivision)
          {
            // Simply replace a with the new value.
            uo.a = n_op.a;
            return;  
          }
        break;
        
        case UndoOp::EnableAllAudioControllers:
          if(uo.type == UndoOp::EnableAllAudioControllers)
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double EnableAllAudioControllers. Ignoring.\n");
            return;  
          }
        break;
        
        case UndoOp::NormalizeMidiDivision:
          if(uo.type == UndoOp::NormalizeMidiDivision)
          {
            fprintf(stderr, "MusE error: Undo::insert(): Double NormalizeMidiDivision. Ignoring.\n");
            return;  
          }
        break;
        
        case UndoOp::GlobalSelectAllEvents:
          if(uo.type == UndoOp::GlobalSelectAllEvents)
          {
            if(uo.a == n_op.a)
            {
              fprintf(stderr, "MusE error: Undo::insert(): Double GlobalSelectAllEvents. Ignoring.\n");
              return;
            }
            else
            {
              // Special: Do not 'cancel' out this one. The selecions may need to affect all events.
              // Simply replace a with the new value.
              uo.a = n_op.a;
              return;  
            }
          }
        break;


        case UndoOp::AddMarker:
          if(uo.type == UndoOp::AddMarker && uo.newMarker->id() == n_op.newMarker->id())
          {
            // Done with older operation marker. Be sure to delete it.
            delete uo.newMarker;
            // Simply replace the existing new marker with the newer marker.
            uo.newMarker = n_op.newMarker;
            return;  
          }
          else if(uo.type == UndoOp::DeleteMarker && uo.oldMarker->id() == n_op.newMarker->id())
          {
            // Delete followed by add. Transform the existing DeleteMarker operation into a ModifyMarker.
            uo.type = UndoOp::ModifyMarker;
            // Move the new marker into the ModifyMarker command's new marker.
            // Keep the existing DeleteMarker command's oldMarker.
            uo.newMarker = n_op.newMarker;
            return;
          }
        break;
        
        case UndoOp::DeleteMarker:
          if(uo.type == UndoOp::DeleteMarker && uo.oldMarker->id() == n_op.oldMarker->id())
          {
            // Done with older operation marker. Be sure to delete it.
            delete uo.oldMarker;
            // Simply replace the existing new marker with the newer marker.
            uo.oldMarker = n_op.oldMarker;
            return;  
          }
          else if(uo.type == UndoOp::AddMarker && uo.newMarker->id() == n_op.oldMarker->id())
          {
            // Done with operation markers. Be sure to delete them.
            delete uo.oldMarker;
            delete n_op.newMarker;
            // Add followed by delete is useless. Cancel out the add + delete by erasing the add command.
            erase(iuo);
            return;  
          }
        break;
        
        case UndoOp::ModifyMarker:
          if(uo.type == UndoOp::ModifyMarker && uo.oldMarker->id() == n_op.oldMarker->id())
          {
            // Done with these operation markers. Be sure to delete them.
            delete uo.newMarker;
            delete n_op.oldMarker;
            // Simply replace the older operation marker with the newer one.
            uo.newMarker = n_op.newMarker;
            return;  
          }
        break;
        
        case UndoOp::SetMarkerPos:
          if(uo.type == UndoOp::SetMarkerPos && uo.oldMarker->id() == n_op.oldMarker->id())
          {
            // Done with these operation markers. Be sure to delete them.
            delete uo.newMarker;
            delete n_op.oldMarker;
            // Simply replace the older operation marker with the newer one.
            uo.newMarker = n_op.newMarker;
            return;  
          }
        break;
        
        // NOTE Some other undo op types may need treatment as well !
        
        default:
        break;  
      }
    }
  }
  
  std::list<UndoOp>::insert(position, n_op);
}

bool Undo::merge_combo(const Undo& other)
{
  if (other.combobreaker)
          return false;
  
  int has_other=0x01;
  int has_select_event=0x02;
  int has_select_part=0x04;
  int has_modify_aud_ctrl_val=0x08;
  int has_set_marker_pos=0x10;

  int has = 0;
  for (ciUndoOp op=this->begin(); op!=this->end(); op++)
          switch(op->type)
          {
                  case UndoOp::DoNothing: break;
                  case UndoOp::SelectEvent: has |= has_select_event; break;
                  case UndoOp::SelectPart: has |= has_select_part; break;
                  case UndoOp::ModifyAudioCtrlVal: has |= has_modify_aud_ctrl_val; break;
                  case UndoOp::SetMarkerPos: has |= has_set_marker_pos; break;
                  default: has |= has_other; break;
          }
  
  for (ciUndoOp op=other.begin(); op!=other.end(); op++)
          switch(op->type)
          {
                  case UndoOp::DoNothing: break;
                  case UndoOp::SelectEvent: has |= has_select_event; break;
                  case UndoOp::SelectPart: has |= has_select_part; break;
                  case UndoOp::ModifyAudioCtrlVal: has |= has_modify_aud_ctrl_val; break;
                  case UndoOp::SetMarkerPos: has |= has_set_marker_pos; break;
                  default: has |= has_other; break;
          }
  
  bool mergeable =
    (has == has_select_event || has == has_select_part ||
     has == has_modify_aud_ctrl_val || has == has_set_marker_pos);
  
  if (mergeable)
          this->insert(this->end(), other.begin(), other.end());
  
  return mergeable;
}

bool Song::applyOperation(const UndoOp& op, OperationType type, void* sender)
{
	Undo operations;
	operations.push_back(op);
	return applyOperationGroup(operations, type, sender);
}

bool Song::applyOperationGroup(Undo& group, OperationType type, void* sender)
{
  bool ret = false;
  if (!group.empty())
  {
    // We don't use this here in applyOperationGroup or its call sequence.
    undoMode = false;

    switch(type)
    {
      case OperationExecute:
      case OperationUndoable:
      break;
      
      case OperationExecuteUpdate:
      case OperationUndoableUpdate:
      case OperationUndoMode:
          // Clear the updateFlags and set sender.
          updateFlags = SongChangedStruct_t(0, 0, sender);
      break;
    }

    // Execute the given operations. This can add or remove operations in the group.
    MusEGlobal::audio->msgExecuteOperationGroup(group);
    
    // Check whether there are actually any undoable operations in the group.
    // There shouldn't be any non-undoables left in the list, they are removed at execution,
    //  but we'll double check here which also checks list emptiness.
    bool has_undoables = false;
    for(ciUndoOp iu = group.cbegin(); iu != group.cend(); ++iu) {
      if(!iu->_noUndo) {
        has_undoables = true;
        break;
      }
    }

    switch(type)
    {
      case OperationExecute:
      case OperationExecuteUpdate:
      break;
        
      case OperationUndoMode:
        // NOTE: If there are only non-undoables, there is NOTHING to redo (or undo).
        //       Prevent one-time non-undoable operations from wiping out the redo list!
        if(has_undoables) {
          // The following does the same as startUndo but without clearing the updateFlags:
          // redo must be invalidated when a new undo is started
          redoList->clearDelete();
          MusEGlobal::redoAction->setEnabled(false);
          setUndoRedoText();
          undoList->push_back(Undo());
        }
      // FALLTHROUGH
      case OperationUndoable:
      case OperationUndoableUpdate:
        // append all elements from "group" to the end of undoList->back().
        // Only if there are undoable items.
        if(has_undoables && !undoList->empty())
        {
          Undo& curUndo = undoList->back();
          curUndo.insert(curUndo.end(), group.begin(), group.end());
          if (group.combobreaker)
            curUndo.combobreaker=true;
        }
      break;
    }

    switch(type)
    {
      case OperationExecute:
      case OperationUndoable:
      break;
      
      case OperationExecuteUpdate:
      case OperationUndoableUpdate:
        emit songChanged(updateFlags);
      break;
      
      case OperationUndoMode:
        if(has_undoables) {
          // Also emits songChanged and resets undoMode.
          endUndo(0);
          ret = true;
        }
        else {
          emit songChanged(updateFlags);
        }
      break;
    }
  }
        
  return ret;
}

//---------------------------------------------------------
//   revertOperationGroup2
//    real time part
//---------------------------------------------------------

void Song::revertOperationGroup2(Undo& /*operations*/)
      {
        pendingOperations.executeRTStage();

        // Special for tempo: Need to normalize the tempo list, and resync audio. 
        // To save time this is done here, not item by item.
        // Normalize is not needed for SC_MASTER.
        if(updateFlags & (SC_TEMPO | SC_DIVISION_CHANGED))
          MusEGlobal::tempomap.normalize();
        if(updateFlags & (SC_TEMPO | SC_MASTER | SC_DIVISION_CHANGED))
        {
          MusEGlobal::audio->reSyncAudio();
          // Must rebuild the marker list in case any markers are 'locked'.
          if(marker()->rebuild())
            updateFlags |= SC_MARKERS_REBUILT;
        }

        // Special for sig: Need to normalize the signature list. 
        // To save time this is done here, not item by item.
        if(updateFlags & (SC_SIG | SC_DIVISION_CHANGED))
          MusEGlobal::sigmap.normalize();

        // Special for track inserted: If it's an aux track, need to add missing aux sends to all tracks,
        //  else if it's another audio track need to add aux sends to it.
        // To save from complexity this is done here, after all the operations.
        if(updateFlags & SC_TRACK_INSERTED)
        {
          int n = _auxs.size();
          for(iTrack i = _tracks.begin(); i != _tracks.end(); ++i) 
          {
            if((*i)->isMidiTrack())
              continue;
            MusECore::AudioTrack* at = static_cast<AudioTrack*>(*i);
            if(at->hasAuxSend()) 
              at->addAuxSend(n);
          }
        }
      }

//---------------------------------------------------------
//   Song::executeOperationGroup2
//---------------------------------------------------------

void Song::executeOperationGroup2(Undo& /*operations*/)
      {
        pendingOperations.executeRTStage();
        
        // Special for tempo if altered: Need to normalize the tempo list, and resync audio. 
        // To save time this is done here, not item by item.
        // Normalize is not needed for SC_MASTER.
        if(updateFlags & (SC_TEMPO | SC_DIVISION_CHANGED))
          MusEGlobal::tempomap.normalize();
        if(updateFlags & (SC_TEMPO | SC_MASTER | SC_DIVISION_CHANGED))
        {
          MusEGlobal::audio->reSyncAudio();
          // Must rebuild the marker list in case any markers are 'locked'.
          if(marker()->rebuild())
            updateFlags |= SC_MARKERS_REBUILT;
        }

        // Special for sig: Need to normalize the signature list. 
        // To save time this is done here, not item by item.
        if(updateFlags & (SC_SIG | SC_DIVISION_CHANGED))
          MusEGlobal::sigmap.normalize();
        
        // Special for track inserted: If it's an aux track, need to add missing aux sends to all tracks,
        //  else if it's another audio track need to add aux sends to it.
        // To save from complexity this is done here, after all the operations.
        if(updateFlags & SC_TRACK_INSERTED)
        {
          int n = _auxs.size();
          for(iTrack i = _tracks.begin(); i != _tracks.end(); ++i) 
          {
            if((*i)->isMidiTrack())
              continue;
            MusECore::AudioTrack* at = static_cast<AudioTrack*>(*i);
            if(at->hasAuxSend()) 
              at->addAuxSend(n);
          }
        }
      }

UndoOp::UndoOp()
{
  type=UndoOp::DoNothing;
  _noUndo = true;
}

UndoOp::UndoOp(UndoType type_, int a_, int b_, int c_, bool noUndo)
      {
      assert(type_==AddKey || type_==DeleteKey || type_== ModifyKey ||
             type_==AddTempo || type_==DeleteTempo || type_==ModifyTempo || 
             type_==SetTempo || type_==SetStaticTempo || type_==SetGlobalTempo || type_==EnableMasterTrack ||
             type_==AddSig || type_==DeleteSig ||
             type_==ModifySongLen || type_==MoveTrack ||
             type_==GlobalSelectAllEvents || type_==ModifyMidiDivision);
      
      type = type_;
      a  = a_;
      b  = b_;
      c  = c_;
      _noUndo = noUndo;
      
      switch(type)
      {
        case UndoOp::SetGlobalTempo:
          // a is already the new tempo, b is the existing tempo.
          b = MusEGlobal::tempomap.globalTempo();
        break;
        
        case UndoOp::EnableMasterTrack:
          // a is already the new master flag, b is the existing master flag.
          b = MusEGlobal::tempomap.masterFlag();
        break;
        
        case UndoOp::ModifyMidiDivision:
          // a is already the new division, b is the existing division.
          b = MusEGlobal::config.division;
        break;
        
        // For these operations, we must check if a value already exists and transform them into modify operations...
        case UndoOp::AddTempo:
        {
          int t = a;
          if(t > MAX_TICK)
            t = MAX_TICK;
          iTEvent ite = MusEGlobal::tempomap.upper_bound(t);
          if((int)ite->second->tick == t)
          {
            // Transform the AddTempo operation into a ModifyTempo.
            // a is already the tick, b is the existing value, c is the new value.
            type = UndoOp::ModifyTempo;
            c = b;
            b = ite->second->tempo;
          }
        }
        break;
        
        case UndoOp::SetTempo:
        {
          // Only if the master is on.
          if(MusEGlobal::tempomap.masterFlag())
          {
            int t = a;
            if(t > MAX_TICK)
              t = MAX_TICK;
            iTEvent ite = MusEGlobal::tempomap.upper_bound(t);
            if((int)ite->second->tick == t)
            {
              // Transform the SetTempo operation into a ModifyTempo.
              // a is already the tick, b is the existing value, c is the new value.
              type = UndoOp::ModifyTempo;
              c = b;
              b = ite->second->tempo;
            }
            else
            {
              // Transform the SetTempo operation into an AddTempo.
              type = UndoOp::AddTempo;
            }
          }
          else
          {
            // a is the new tempo, b is the existing tempo.
            a = b;
            b = MusEGlobal::tempomap.staticTempo();
            // Transform the SetTempo operation into a SetStaticTempo.
            type = UndoOp::SetStaticTempo;
          }
        }
        break;
        
        case UndoOp::SetStaticTempo:
          // a is already the new tempo, b is the existing tempo.
          b = MusEGlobal::tempomap.staticTempo();
        break;
        
        case UndoOp::AddSig:
        {
          //if(t > MAX_TICK)
          //  t = MAX_TICK;
          
          // Must rasterize the tick value HERE instead of in SigMap::addOperation(),
          //  so that the rasterized value is recorded in the undo item.
          a = MusEGlobal::sigmap.raster1(a, 0);
          
          MusECore::iSigEvent ise = MusEGlobal::sigmap.upper_bound(a);
          if((int)ise->second->tick == a)
          {
            // Transform the AddSig operation into a ModifySig.
            // a is already the tick, b + c is the existing value, d + e is the new value.
            type = UndoOp::ModifySig;
            d = b;
            e = c;
            b = ise->second->sig.z;
            c = ise->second->sig.n;
          }
        }
        break;

        case UndoOp::AddKey:
        {
          int t = a;
          if(t > MAX_TICK)
            t = MAX_TICK;
          iKeyEvent ike = MusEGlobal::keymap.upper_bound(t);
          if((int)ike->second.tick == t)
          {
            // Transform the AddKey operation into a ModifyKey.
            // a is already the tick, b + c is the existing value, d + e is the new value.
            type = UndoOp::ModifyKey;
            d = b;
            e = c;
            b = ike->second.key;
            c = ike->second.minor;
          }
        }
        break;
        
        default:
        break;
      }
      
      }

UndoOp::UndoOp(UndoType type_, int tick, const MusECore::TimeSignature old_sig, const MusECore::TimeSignature new_sig, bool noUndo)
{
      assert(type_==ModifySig);
      type    = type_;
      a  = tick;
      b  = old_sig.z;
      c  = old_sig.n;
      d  = new_sig.z;
      e  = new_sig.n;
      _noUndo = noUndo;
}

UndoOp::UndoOp(UndoType type_, int n, const Track* track_, bool noUndo)
      {
      assert(type_==AddTrack || type_==DeleteTrack);
      assert(track_);
      
      type    = type_;
      trackno = n;
      track  = track_;
      _noUndo = noUndo;
      }

UndoOp::UndoOp(UndoType type_, const Track* track_, bool value, bool noUndo)
      {
      assert(type_ == SetTrackRecord || type_ == SetTrackMute || type_ == SetTrackSolo || 
             type_ == SetTrackRecMonitor || type_ == SetTrackOff);
      assert(track_);
      
      type    = type_;
      track  = track_;
      a = value;
      _noUndo = noUndo;
      }

UndoOp::UndoOp(UndoType type_, const Part* part_, bool noUndo)
      {
      assert(type_==AddPart || type_==DeletePart);
      assert(part_);
      
      type  = type_;
      part = part_;
      _noUndo = noUndo;
      }
      
UndoOp::UndoOp(UndoType type_, const Part* part_, bool selected_, bool sel_old_, bool noUndo)
{
    assert(type_==SelectPart);
    assert(part_);
    
    type=type_;
    part = part_;
    selected=selected_;
    selected_old=sel_old_;
    _noUndo = noUndo;
}

UndoOp::UndoOp(UndoType type_, const Part* part_, unsigned int old_len_or_pos, unsigned int new_len_or_pos,
               Pos::TType new_time_type_, const Track* oTrack, const Track* nTrack, bool noUndo)
{
    assert(type_== MovePart);
    assert(part_);

    type = type_;
    part = part_;
    _noUndo = noUndo;
    track = nTrack;
    oldTrack = oTrack;
    // Make sure both tracks exist.
    if(!track && !oldTrack)
      track = oldTrack = part->track();
    else if(!oldTrack)
      oldTrack = track;
    else if(!track)
      track = oldTrack;
    assert(oldTrack);
    assert(track);
    old_partlen_or_pos = old_len_or_pos;
    new_partlen_or_pos = new_len_or_pos;
    switch(part->type())
    {
      case Pos::FRAMES:
        switch(new_time_type_)
        {
          case Pos::FRAMES:
          break;
          
          case Pos::TICKS:
            new_partlen_or_pos = MusEGlobal::tempomap.tick2frame(new_partlen_or_pos);
          break;  
        }
      break;
      
      case Pos::TICKS:
        switch(new_time_type_)
        {
          case Pos::FRAMES:
            new_partlen_or_pos = MusEGlobal::tempomap.frame2tick(new_partlen_or_pos);
          break;

          case Pos::TICKS:
          break;  
        }
      break;
    }
}


UndoOp::UndoOp(UndoType type_, const Part* part_, unsigned int old_pos, unsigned int new_pos, unsigned int old_len, unsigned int new_len,
               int64_t events_offset_, Pos::TType new_time_type_, bool noUndo)
{
    assert(type_ == ModifyPartStart);
    assert(part_);

    type = type_;
    part = part_;
    _noUndo = noUndo;
    events_offset = events_offset_;
    events_offset_time_type = new_time_type_;
    old_partlen_or_pos = old_pos;
    new_partlen_or_pos = new_pos;
    old_partlen = old_len;
    new_partlen = new_len;
}

UndoOp::UndoOp(UndoType type_, const Part* part_, unsigned int old_len, unsigned int new_len,
               int64_t events_offset_, Pos::TType new_time_type_, bool noUndo)
{
    assert(type_== ModifyPartLength);
    assert(part_);

    type = type_;
    part = part_;
    _noUndo = noUndo;
    events_offset = events_offset_;
    events_offset_time_type = new_time_type_;
    old_partlen_or_pos = old_len;
    new_partlen_or_pos = new_len;
}

UndoOp::UndoOp(UndoType type_, const Event& nev, const Event& oev, const Part* part_, bool doCtrls_, bool doClones_, bool noUndo)
      {
      assert(type_==ModifyEvent);
      assert(part_);
      
      type   = type_;
      nEvent = nev;
      oEvent = oev;
      part   = part_;
      doCtrls = doCtrls_;
      doClones = doClones_;
      _noUndo = noUndo;
      }

UndoOp::UndoOp(UndoType type_, const Event& nev, const Part* part_, bool a_, bool b_, bool noUndo)
      {
      assert(type_==DeleteEvent || type_==AddEvent || type_==SelectEvent);
      assert(part_);
      
      type   = type_;
      nEvent = nev;
      part   = part_;
      _noUndo = noUndo;
      if(type_==SelectEvent)
      {
        selected = a_;
        selected_old = b_;
      }
      else
      {
        doCtrls = a_;
        doClones = b_;
      }
      }
      
UndoOp::UndoOp(UndoType type_, const Marker& oldMarker_, const Marker& newMarker_, bool noUndo)
      {
      assert(type_==ModifyMarker);
      type    = type_;
      oldMarker  = new Marker(oldMarker_);
      newMarker = new Marker(newMarker_);
      _noUndo = noUndo;
      }

UndoOp::UndoOp(UndoType type_, const Marker& marker_, bool noUndo)
      {
      assert(type_==AddMarker || type_==DeleteMarker);
      type    = type_;
      oldMarker = newMarker = nullptr;
      Marker** mp = nullptr;
      if(type_== AddMarker)
        mp = &newMarker;
      else
        mp = &oldMarker;
      *mp = new Marker(marker_);
      _noUndo = noUndo;
      }

UndoOp::UndoOp(UndoType type_, const Marker& marker_, unsigned int new_pos, Pos::TType new_time_type, bool noUndo)
      {
      assert(type_==SetMarkerPos);
      type    = type_;
      oldMarker = new Marker(marker_);
      newMarker = new Marker(marker_);
      newMarker->setPosValue(new_pos, new_time_type);
      _noUndo = noUndo;
      }

// UndoOp::UndoOp(UndoType type_, MarkerList** oldMarkerList_, MarkerList* newMarkerList_, bool noUndo)
//       {
//       assert(type_==ModifyMarkerList);
//       assert(oldMarkerList);
//       assert(newMarkerList);
//       type    = type_;
//       oldMarkerList = oldMarkerList_;
//       newMarkerList = newMarkerList_;
//       _noUndo = noUndo;
//       }
      
UndoOp::UndoOp(UndoType type_, const Event& changedEvent, const QString& changeData, int startframe_, int endframe_, bool noUndo)
      {
      assert(type_==ModifyClip);
      
      type = type_;
      _noUndo = noUndo;
      //filename   = new QString(changedFile);
      nEvent = changedEvent;
      tmpwavfile = new QString(changeData);
      startframe = startframe_;
      endframe   = endframe_;
      }

UndoOp::UndoOp(UndoOp::UndoType type_, const Part* part_, const QString& old_name, const QString& new_name, bool noUndo)
{
    assert(type_==ModifyPartName);
    assert(part_);
    
    type=type_;
    part=part_;
    _noUndo = noUndo;
    _oldName = new QString(old_name);
    _newName = new QString(new_name);
}

UndoOp::UndoOp(UndoOp::UndoType type_, const Track* track_, const QString& old_name, const QString& new_name, bool noUndo)
{
  assert(type_==ModifyTrackName);
  assert(track_);
    
  type = type_;
  track = track_;
  _noUndo = noUndo;
  _oldName = new QString(old_name);
  _newName = new QString(new_name);
}

UndoOp::UndoOp(UndoOp::UndoType type_, const Track* track_, int oldChanOrCtrlID, int newChanOrCtrlFrame, bool noUndo)
{
  assert(type_ == ModifyTrackChannel || type_ == DeleteAudioCtrlVal);
  assert(track_);
  
  type = type_;
  track = track_;
  
  if(type_ == ModifyTrackChannel)
  {
    _propertyTrack = track_;
    _oldPropValue = oldChanOrCtrlID;
    _newPropValue = newChanOrCtrlFrame;
  }
  else
  {
    _audioCtrlID = oldChanOrCtrlID;
    _audioCtrlFrame = newChanOrCtrlFrame;
  }
  _noUndo = noUndo;
}

UndoOp::UndoOp(UndoType type_, const Track* track_, int ctrlID, int frame, double value, bool noUndo)
{
  assert(type_== AddAudioCtrlVal);
  assert(track_);
  
  type = type_;
  track = track_;
  _audioCtrlID = ctrlID;
  _audioCtrlFrame = frame;
  _audioCtrlVal = value;
  _noUndo = noUndo;
}

UndoOp::UndoOp(UndoType type_, const Track* track_, int ctrlID, int oldFrame, int newFrame, double oldValue, double newValue, bool noUndo)
{
  assert(type_== ModifyAudioCtrlVal);
  assert(track_);
  
  type = type_;
  track = track_;
  _audioCtrlID = ctrlID;
  _audioCtrlFrame = oldFrame;
  _audioNewCtrlFrame = newFrame;
  _audioCtrlVal = oldValue;
  _audioNewCtrlVal = newValue;
  _noUndo = noUndo;
}

UndoOp::UndoOp(UndoOp::UndoType type_, CtrlListList* ctrl_ll, CtrlList* eraseCtrlList, CtrlList* addCtrlList, bool noUndo)
{
  assert(type_== ModifyAudioCtrlValList);
  assert(ctrl_ll);
  //assert(eraseCtrlList);
  //assert(addCtrlList);
  assert(eraseCtrlList || addCtrlList);
  
  type = type_;
  _ctrlListList = ctrl_ll;
  _eraseCtrlList = eraseCtrlList;
  _addCtrlList = addCtrlList;
  _noUndo = noUndo;
}

UndoOp::UndoOp(UndoType type_, MidiPort* mp, MidiInstrument* instr, bool noUndo)
{
  assert(type_== SetInstrument);
  assert(mp);
  assert(instr);
  type = type_;
  _midiPort = mp;
  _oldMidiInstrument = _midiPort->instrument();
  _newMidiInstrument = instr;
  _noUndo = noUndo;
}

UndoOp::UndoOp(UndoOp::UndoType type_)
{
  assert(type_== EnableAllAudioControllers || type_ == NormalizeMidiDivision);
  
  type = type_;
  // Cannot be undone. 'One-time' operation only, removed after execution.
  _noUndo = true;
}
      
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
UndoOp::UndoOp(UndoOp::UndoType type_, const Route& route_from_, const Route& route_to_, bool noUndo)
      {
      assert(type_ == AddRoute || type_ == DeleteRoute);
      _noUndo = noUndo;
      routeFrom = route_from_;
      routeTo = route_to_;
      }
#pragma GCC diagnostic pop

//---------------------------------------------------------
//   addUndo
//---------------------------------------------------------

void Song::addUndo(UndoOp i)
      {
      if (!undoMode) {
            printf("internal error: undoOp without startUndo()\n");
            return;
            }
      undoList->back().push_back(i);
      emit sigDirty();
      }

//---------------------------------------------------------
//   revertOperationGroup1
//    non realtime context
//    return true if nothing to do
//---------------------------------------------------------

void Song::revertOperationGroup1(Undo& operations)
      {
      MarkerList* new_marker_list = nullptr;
      TempoList* new_tempo_list = nullptr;
      SigList* new_sig_list = nullptr;
      KeyList* new_key_list = nullptr;
      
      for (riUndoOp i = operations.rbegin(); i != operations.rend(); ++i) {
            Track* editable_track = const_cast<Track*>(i->track);
            Track* editable_property_track = const_cast<Track*>(i->_propertyTrack);
            Part* editable_part = const_cast<Part*>(i->part);
            switch(i->type) {
                  case UndoOp::SelectPart:
                        pendingOperations.add(PendingOperationItem(editable_part, i->selected_old, PendingOperationItem::SelectPart));
                        updateFlags |= SC_PART_SELECTION;
                        break;
                  case UndoOp::SelectEvent:
                        pendingOperations.add(PendingOperationItem(editable_part, i->nEvent, i->selected_old, PendingOperationItem::SelectEvent));
                        updateFlags |= SC_SELECTION;
                        break;
                        
                  case UndoOp::AddTrack:
                        switch(editable_track->type())
                        {
                          case Track::AUDIO_SOFTSYNTH:
                          {
                            SynthI* si = (SynthI*)editable_track;
                            if(si->hasGui())
                              si->showGui(false);
                            if(si->hasNativeGui())       
                              si->showNativeGui(false);
                          }// Fall through.
                          case Track::WAVE:
                          case Track::AUDIO_OUTPUT:
                          case Track::AUDIO_INPUT:
                          case Track::AUDIO_GROUP:
                          case Track::AUDIO_AUX:
                            ((AudioTrack*)editable_track)->deleteAllEfxGuis();
                            updateFlags |= SC_RACK;
                          break;
                          
                          default:
                          break;
                        }
                        
                        switch(editable_track->type())
                        {
                          case Track::AUDIO_OUTPUT:
                          {
                            AudioOutput* ao = (AudioOutput*)editable_track;
                            for(int ch = 0; ch < ao->channels(); ++ch)
                            {
                              MusEGlobal::audioDevice->unregisterPort(ao->jackPort(ch));
                              //ao->setJackPort(ch, 0);  // Done in RT stage.
                              updateFlags |= SC_ROUTE;
                            }
                          }
                          break;
                          
                          case Track::AUDIO_INPUT:
                          {
                            AudioOutput* ai = (AudioOutput*)editable_track;
                            for(int ch = 0; ch < ai->channels(); ++ch)
                            {
                              MusEGlobal::audioDevice->unregisterPort(ai->jackPort(ch));
                              //ai->setJackPort(ch, 0); // Done in RT stage.
                              updateFlags |= SC_ROUTE;
                            }
                          }     
                          break;
                          
                          case Track::AUDIO_AUX:
                            updateFlags |= SC_AUX;
                          break;
                          
                          default:
                          break;
                        }
                        removeTrackOperation(editable_track, pendingOperations);
                        updateFlags |= SC_TRACK_REMOVED;
                        break;
                        
                  case UndoOp::DeleteTrack:
                        switch(editable_track->type())
                        {
                          case Track::AUDIO_SOFTSYNTH:
                          {
                            SynthI* s = (SynthI*)editable_track;
                            Synth* sy = s->synth();
                            if(!s->isActivated()) 
                              s->initInstance(sy, s->name());
                            // FIXME TODO: We want to restore any ports using this instrument via the undo
                            //  system but ATM a few other things can set the instrument without an undo
                            //  operation so the undo sequence would not be correct. So we don't have much
                            //  choice but to just reset inside the PendingOperation::DeleteTrack operation
                            //  for now when deleting a synth track.
                            // Still, everything else is in place for undoable setting of instrument...
                          }
                          break;
                                
                          case Track::AUDIO_OUTPUT:
                          {
                            AudioOutput* ao = (AudioOutput*)editable_track;
                            if(MusEGlobal::checkAudioDevice())
                            {
                              for(int ch = 0; ch < ao->channels(); ++ch) 
                              {
                                // This should be OK since the track has not yet been added in the realtime stage.
                                if(ao->registerPorts(ch))
                                  updateFlags |= SC_ROUTE;
                                
                                // Set the route Jack ports now to relieve our graph callback handler from having to do it.
                                RouteList* rl = ao->outRoutes();
                                for(iRoute ir = rl->begin(); ir != rl->end(); ++ir)
                                  if(ir->type == Route::JACK_ROUTE && ir->channel == ch)
                                  {
                                    ir->jackPort = MusEGlobal::audioDevice->findPort(ir->persistentJackPortName);
                                    updateFlags |= SC_ROUTE;
                                  }
                              }
                            }
                          }
                          break;
                              
                          case Track::AUDIO_INPUT:
                          {
                            AudioInput* ai = (AudioInput*)editable_track;
                            if(MusEGlobal::checkAudioDevice())
                            {
                              for(int ch = 0; ch < ai->channels(); ++ch) 
                              {
                                // This should be OK since the track has not yet been added in the realtime stage.
                                if(ai->registerPorts(ch))
                                  updateFlags |= SC_ROUTE;
                                
                                // Set the route Jack ports now to relieve our graph callback handler from having to do it.
                                RouteList* rl = ai->inRoutes();
                                for(iRoute ir = rl->begin(); ir != rl->end(); ++ir)
                                  if(ir->type == Route::JACK_ROUTE && ir->channel == ch)
                                  {
                                    ir->jackPort = MusEGlobal::audioDevice->findPort(ir->persistentJackPortName);
                                    updateFlags |= SC_ROUTE;
                                  }
                              }
                            }
                          }
                          break;
                          
                          case Track::AUDIO_AUX:
                            updateFlags |= SC_AUX;
                          break;
                          
                          default:
                          break;
                        }
                    
                        // Ensure that wave event sndfile file handles are opened.
                        // It should not be the job of the pending operations list to do this.
                        // TODO Coordinate close/open with part mute and/or track off.
                        editable_track->openAllParts();
                        
                        insertTrackOperation(editable_track, i->trackno, pendingOperations);
                        updateFlags |= SC_TRACK_INSERTED;
                        break;
                        
                  case UndoOp::ModifyClip:
                        sndFileApplyUndoFile(i->nEvent, i->tmpwavfile, i->startframe, i->endframe);
                        updateFlags |= SC_CLIP_MODIFIED;
                        break;
                  case UndoOp::ModifyTrackChannel:
                        if (editable_property_track->isMidiTrack()) 
                        {
                          MusECore::MidiTrack* mt = static_cast<MusECore::MidiTrack*>(editable_property_track);
                          if (i->_oldPropValue != mt->outChannel()) 
                          {
                                MusECore::MidiTrack::ChangedType_t changed = MusECore::MidiTrack::NothingChanged;
                                MusEGlobal::audio->msgIdle(true);
                                changed |= mt->setOutChanAndUpdate(i->_oldPropValue, false);
                                MusEGlobal::audio->msgIdle(false);
                                MusEGlobal::audio->msgUpdateSoloStates();                   
                                updateFlags |= (SC_ROUTE | ((changed & MusECore::MidiTrack::DrumMapChanged) ? SC_DRUMMAP : 0));
                          }
                        }
                        else
                        {
                            if(editable_property_track->type() != MusECore::Track::AUDIO_SOFTSYNTH)
                            {
                              MusECore::AudioTrack* at = static_cast<MusECore::AudioTrack*>(editable_property_track);
                              if (i->_oldPropValue != at->channels()) {
                                    MusEGlobal::audio->msgSetChannels(at, i->_oldPropValue);
                                    updateFlags |= SC_CHANNELS;
                                    }
                            }         
                        }      
                        break;

                  case UndoOp::SetTrackRecord:
                        if(!editable_track->setRecordFlag1(!i->a))
                          break;
                        pendingOperations.add(PendingOperationItem(editable_track, !i->a, PendingOperationItem::SetTrackRecord));
                        // FIXME: No choice but to include monitor flag. Really should try to merge pending ops flags
                        //  with undo flags after executing the pending ops in revertOperationGroup3...
                        updateFlags |= (SC_RECFLAG | SC_TRACK_REC_MONITOR);
                        break;

                  case UndoOp::SetTrackMute:
                        pendingOperations.add(PendingOperationItem(editable_track, !i->a, PendingOperationItem::SetTrackMute));
                        updateFlags |= SC_MUTE;
                        break;

                  case UndoOp::SetTrackSolo:
                        pendingOperations.add(PendingOperationItem(editable_track, !i->a, PendingOperationItem::SetTrackSolo));
                        updateFlags |= SC_SOLO;
                        break;

                  case UndoOp::SetTrackRecMonitor:
                        pendingOperations.add(PendingOperationItem(editable_track, !i->a, PendingOperationItem::SetTrackRecMonitor));
                        updateFlags |= SC_TRACK_REC_MONITOR;
                        break;

                  case UndoOp::SetTrackOff:
                        pendingOperations.add(PendingOperationItem(editable_track, !i->a, PendingOperationItem::SetTrackOff));
                        updateFlags |= SC_MUTE;
                        break;

                        
                  case UndoOp::AddRoute:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:AddRoute\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(i->routeFrom, i->routeTo, PendingOperationItem::DeleteRoute)); 
                        updateFlags |= SC_ROUTE;
                        break;
                        
                  case UndoOp::DeleteRoute:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:DeleteRoute\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(i->routeFrom, i->routeTo, PendingOperationItem::AddRoute)); 
                        updateFlags |= SC_ROUTE;
                        break;
                        
                  case UndoOp::ModifyTrackName:
                        pendingOperations.add(PendingOperationItem(editable_track, i->_oldName, PendingOperationItem::ModifyTrackName));
                        updateFlags |= (SC_TRACK_MODIFIED | SC_MIDI_TRACK_PROP);
                        // If it's an aux track, notify aux UI controls to reload, or change their names etc.
                        if(editable_track->type() == Track::AUDIO_AUX)
                          updateFlags |= SC_AUX;
                        break;
                        
                  case UndoOp::MoveTrack:
                        pendingOperations.add(PendingOperationItem(&_tracks, i->b, i->a, PendingOperationItem::MoveTrack));
                        updateFlags |= SC_TRACK_MOVED;
                        break;
                        
                  case UndoOp::ModifyPartName:
                        pendingOperations.add(PendingOperationItem(editable_part, i->_oldName, PendingOperationItem::ModifyPartName));
                        updateFlags |= SC_PART_MODIFIED;
                        break;
                        
                  case UndoOp::ModifyPartLength: 
                        {
                        pendingOperations.modifyPartLengthOperation(
                          editable_part, i->old_partlen_or_pos, -i->events_offset, i->events_offset_time_type);
                        updateFlags |= SC_PART_MODIFIED;
                        // If the part had events, then treat it as if they were added/removed with separate Add/DeleteEvent operations.
                        // Even if they will be added/deleted later in this operations group with actual separate Add/DeleteEvent operations,
                        //  that's an SC_EVENT_ADDED/REMOVED anyway, so hopefully no harm.
                        if(i->events_offset != 0 && !editable_part->events().empty())
                          updateFlags |= (SC_EVENT_INSERTED | SC_EVENT_REMOVED | SC_EVENT_MODIFIED);
                        }
                        break;
                  case UndoOp::ModifyPartStart:
                        {
                        pendingOperations.modifyPartStartOperation(
                          editable_part, i->old_partlen_or_pos, i->old_partlen, -i->events_offset, i->events_offset_time_type);
                        updateFlags |= SC_PART_MODIFIED;
                        // If the part had events, then treat it as if they were added/removed with separate Add/DeleteEvent operations.
                        // Even if they will be added/deleted later in this operations group with actual separate Add/DeleteEvent operations,
                        //  that's an SC_EVENT_ADDED/REMOVED anyway, so hopefully no harm.
                        if(i->events_offset != 0 && !editable_part->events().empty())
                          updateFlags |= (SC_EVENT_INSERTED | SC_EVENT_REMOVED | SC_EVENT_MODIFIED);
                        }
                        break;

                  case UndoOp::MovePart:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:MovePart ** calling parts->movePartOperation\n");
#endif                        
                        pendingOperations.movePartOperation(editable_part->track()->parts(),
                          editable_part, i->old_partlen_or_pos, const_cast<Track*>(i->oldTrack));
                        if(const_cast<Track*>(i->oldTrack))
                          updateFlags |= SC_PART_INSERTED | SC_PART_REMOVED;
                        updateFlags |= SC_PART_MODIFIED;
                        break;
                        
                  case UndoOp::AddPart:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:AddPart ** calling parts->delOperation\n");
#endif                        
                        pendingOperations.delPartOperation(editable_part->track()->parts(), editable_part);
                        updateFlags |= SC_PART_REMOVED;
                        // If the part had events, then treat it as if they were removed with separate DeleteEvent operations.
                        // Even if they will be deleted later in this operations group with actual separate DeleteEvent operations,
                        //  that's an SC_EVENT_REMOVED anyway, so hopefully no harm. This fixes a problem with midi controller canvas
                        //  not updating after such a 'delete part with events, no separate AddEvents were used when creating the part'.
                        if(!editable_part->events().empty())
                          updateFlags |= SC_EVENT_REMOVED;
                        break;
                    
                  case UndoOp::DeletePart:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:DeletePart ** calling parts->addOperation\n");
#endif                        
                        // Ensure that wave event sndfile file handles are opened.
                        // It should not be the job of the pending operations list to do this.
                        // TODO Coordinate close/open with part mute and/or track off.
                        editable_part->openAllEvents();
                        
                        pendingOperations.addPartOperation(editable_part->track()->parts(), editable_part);
                        updateFlags |= SC_PART_INSERTED;
                        // If the part has events, then treat it as if they were inserted with separate AddEvent operations.
                        // Even if some will be inserted later in this operations group with actual separate AddEvent operations,
                        //  that's an SC_EVENT_INSERTED anyway, so should be no harm.
                        if(!editable_part->events().empty())
                          updateFlags |= SC_EVENT_INSERTED;
                        break;

                        
                  case UndoOp::AddEvent:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:AddEvent ** calling deleteEvent\n");
#endif                        
                        deleteEventOperation(i->nEvent, editable_part, i->doCtrls, i->doClones);
                        updateFlags |= SC_EVENT_REMOVED;
                        break;

                  case UndoOp::DeleteEvent:
                        {
#ifdef _UNDO_DEBUG_
                          fprintf(stderr, "Song::revertOperationGroup1:DeleteEvent ** calling addEvent\n");
#endif                        
                          if(!i->nEvent.empty())
                          {
                            SndFileR f = i->nEvent.sndFile();
                            // Ensure that wave event sndfile file handle is opened.
                            // It should not be the job of the pending operations list to do this.
                            // TODO Coordinate close/open with part mute and/or track off.
                            if(!f.isNull() && !f.isOpen())
                              f->openRead();
                          }
                          
                          addEventOperation(i->nEvent, editable_part, i->doCtrls, i->doClones);
                          updateFlags |= SC_EVENT_INSERTED;
                        }
                        break;
                        
                  case UndoOp::ModifyEvent:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:ModifyEvent ** calling changeEvent\n");
#endif                        
                        changeEventOperation(i->nEvent, i->oEvent, editable_part, i->doCtrls, i->doClones);
                        updateFlags |= SC_EVENT_MODIFIED;
                        break;

                        
                  case UndoOp::AddAudioCtrlVal:
                  {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:AddAudioCtrlVal\n");
#endif                        
                        CtrlListList* cll = static_cast<AudioTrack*>(editable_track)->controller();
                        iCtrlList icl = cll->find(i->_audioCtrlID);
                        if(icl != cll->end())
                        {
                          CtrlList* cl = icl->second;
                          iCtrl ic = cl->find(i->_audioCtrlFrame);
                          if(ic != cl->end())
                          {
                            pendingOperations.add(PendingOperationItem(cl, ic, PendingOperationItem::DeleteAudioCtrlVal));
                            updateFlags |= SC_AUDIO_CONTROLLER;
                          }
                        }
                  }
                  break;

                  case UndoOp::DeleteAudioCtrlVal:
                  {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:DeleteAudioCtrlVal\n");
#endif                        
                        CtrlListList* cll = static_cast<AudioTrack*>(editable_track)->controller();
                        iCtrlList icl = cll->find(i->_audioCtrlID);
                        if(icl != cll->end())
                        {
                          //CtrlList* cl = icl->second;
                          //iCtrl ic = cl->find(i->_audioCtrlFrame);
                          //if(ic != cl->end())
                          //  // An existing value was found (really shouldn't happen!). Replace it with the old value.
                          //  pendingOperations.add(PendingOperationItem(icl->second, ic, i->_audioCtrlVal, PendingOperationItem::ModifyAudioCtrlVal));
                          //else
                            // Restore the old value.
                            pendingOperations.add(PendingOperationItem(icl->second, i->_audioCtrlFrame, i->_audioCtrlVal, PendingOperationItem::AddAudioCtrlVal));
                          updateFlags |= SC_AUDIO_CONTROLLER;
                        }
                  }
                  break;
                        
                  case UndoOp::ModifyAudioCtrlVal:
                  {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:ModifyAudioCtrlVal\n");
#endif                        
                        CtrlListList* cll = static_cast<AudioTrack*>(editable_track)->controller();
                        iCtrlList icl = cll->find(i->_audioCtrlID);
                        if(icl != cll->end())
                        {
                          CtrlList* cl = icl->second;
                          iCtrl ic = cl->find(i->_audioNewCtrlFrame);
                          if(ic != cl->end())
                          {
                            // Restore the old value.
                            pendingOperations.add(PendingOperationItem(icl->second, ic, i->_audioCtrlFrame, i->_audioCtrlVal, PendingOperationItem::ModifyAudioCtrlVal));
                            updateFlags |= SC_AUDIO_CONTROLLER;
                          }
                        }
                  }
                  break;
                        
                  case UndoOp::ModifyAudioCtrlValList:
                  {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:ModifyAudioCtrlValList\n");
#endif                        
                        // Take either id. At least one list must be valid.
                        const int id = i->_eraseCtrlList ? i->_eraseCtrlList->id() : i->_addCtrlList->id();
                        iCtrlList icl = i->_ctrlListList->find(id);
                        if(icl != i->_ctrlListList->end())
                        {
                          // Make a complete copy of the controller list. The list will be quickly switched in the realtime stage.
                          // The Pending Operations system will take 'ownership' of this and delete it at the appropriate time.
                          CtrlList* new_list = new CtrlList(*icl->second, CtrlList::ASSIGN_PROPERTIES | CtrlList::ASSIGN_VALUES);
                          
                          // Erase any items in the add list that were added...
                          //if(i->_addCtrlList)
                          if(i->_addCtrlList && !i->_addCtrlList->empty())
                          {
                            //const std::size_t sz = i->_addCtrlList->size();
                            //if(sz != 0)
                            //{
                              //const CtrlList& cl_r = *i->_addCtrlList;
                              // Both of these should be valid.
                              //ciCtrl n_s = new_list->find(cl_r[0].frame);      // The first item to be erased.
                              //ciCtrl n_e = new_list->find(cl_r[sz - 1].frame); // The last item to be erased.
                              iCtrl n_s = new_list->find(i->_addCtrlList->begin()->second.frame); // The first item to be erased.
                              ciCtrl e_e = i->_addCtrlList->end();
                              --e_e;
                              //ciCtrl n_e = new_list->find((--i->_eraseCtrlList->end())->second.frame); // The last item to be erased.
                              iCtrl n_e = new_list->find(e_e->second.frame); // The last item to be erased.
                              if(n_s != new_list->end() && n_e != new_list->end())
                              {
                                // Since std range does NOT include the last iterator, increment n_e so erase will get all items.
                                ++n_e;
                                new_list->erase(n_s, n_e);
                              }
                            //}
                          }
                          
                          // Re-add any items in the erase list that were erased...
                          if(i->_eraseCtrlList && !i->_eraseCtrlList->empty())
                            new_list->insert(i->_eraseCtrlList->begin(), i->_eraseCtrlList->end());
                          
                          // The operation will quickly switch the list in the RT stage then the delete the old list in the non-RT stage.
                          pendingOperations.add(PendingOperationItem(icl, new_list, PendingOperationItem::ModifyAudioCtrlValList));
                          updateFlags |= SC_AUDIO_CONTROLLER_LIST;
                        }
                  }
                  break;


                  case UndoOp::SetInstrument:
                  {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:SetInstrument\n");
#endif                        
                        // Restore the old value.
                        pendingOperations.add(PendingOperationItem(
                          i->_midiPort, i->_oldMidiInstrument,
                          PendingOperationItem::SetInstrument));
                        updateFlags |= SC_MIDI_INSTRUMENT;
                  }
                  break;


                  case UndoOp::DeleteTempo:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:DeleteTempo ** calling tempomap.addOperation tick:%d tempo:%d\n", i->a, i->b);
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_tempo_list)
                        {
                          new_tempo_list = new TempoList();
                          new_tempo_list->copy(MusEGlobal::tempomap);
                        }
                        // Defer normalize until end of stage 2.
                        new_tempo_list->addTempo(i->a, i->b, false);

                        updateFlags |= SC_TEMPO;
                        break;
                        
                  case UndoOp::AddTempo:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:AddTempo ** calling tempomap.delOperation tick:%d\n", i->a);
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_tempo_list)
                        {
                          new_tempo_list = new TempoList();
                          new_tempo_list->copy(MusEGlobal::tempomap);
                        }
                        // Defer normalize until end of stage 2.
                        new_tempo_list->delTempo(i->a, false);

                        updateFlags |= SC_TEMPO;
                        break;
                        
                  case UndoOp::ModifyTempo:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:ModifyTempo ** calling tempomap.addOperation tick:%d tempo:%d\n", i->a, i->b);
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_tempo_list)
                        {
                          new_tempo_list = new TempoList();
                          new_tempo_list->copy(MusEGlobal::tempomap);
                        }
                        // Defer normalize until end of stage 2.
                        new_tempo_list->addTempo(i->a, i->b, false);

                        updateFlags |= SC_TEMPO;
                        break;
                        
//                   case UndoOp::SetTempo:
//                         // Only if the master is on.
//                         if(MusEGlobal::tempomap.masterFlag())
//                         {
// #ifdef _UNDO_DEBUG_
//                           fprintf(stderr, "Song::revertOperationGroup1:SetTempo ** calling tempomap.delOperation tick:%d\n", i->a);
// #endif                        
//                           MusEGlobal::tempomap.delOperation(i->a, pendingOperations);
//                         }
//                         else
//                           pendingOperations.add(PendingOperationItem(&MusEGlobal::tempomap, i->b, PendingOperationItem::SetStaticTempo));
//                         updateFlags |= SC_TEMPO;
//                         break;
                        
                  case UndoOp::SetStaticTempo:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:SetStaticTempo ** adding SetStaticTempo operation\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(&MusEGlobal::tempomap, i->b, PendingOperationItem::SetStaticTempo));
                        updateFlags |= SC_TEMPO;
                        break;
                        
                  case UndoOp::SetGlobalTempo:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:SetGlobalTempo ** adding SetGlobalTempo operation\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(&MusEGlobal::tempomap, i->b, PendingOperationItem::SetGlobalTempo));
                        updateFlags |= SC_TEMPO;
                        break;
                        
                  case UndoOp::EnableMasterTrack:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:EnableMasterTrack ** adding EnableMasterTrack operation\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(&MusEGlobal::tempomap, (bool)i->b, PendingOperationItem::SetUseMasterTrack));
                        updateFlags |= SC_MASTER;
                        break;
                        
                  case UndoOp::DeleteSig:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:DeleteSig ** calling MusEGlobal::sigmap.addOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_sig_list)
                        {
                          new_sig_list = new SigList();
                          new_sig_list->copy(MusEGlobal::sigmap);
                        }
                        // Defer normalize until end of stage 2.
                        new_sig_list->add(i->a, MusECore::TimeSignature(i->b, i->c), false);

                        updateFlags |= SC_SIG;
                        break;
                        
                  case UndoOp::AddSig:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:AddSig ** calling MusEGlobal::sigmap.delOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_sig_list)
                        {
                          new_sig_list = new SigList();
                          new_sig_list->copy(MusEGlobal::sigmap);
                        }
                        // Defer normalize until end of stage 2.
                        new_sig_list->del(i->a, false);

                        updateFlags |= SC_SIG;
                        break;
                        
                  case UndoOp::ModifySig:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:ModifySig ** calling MusEGlobal::sigmap.addOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_sig_list)
                        {
                          new_sig_list = new SigList();
                          new_sig_list->copy(MusEGlobal::sigmap);
                        }
                        // Defer normalize until end of stage 2.
                        // TODO: Hm should that be ->d and ->e like in executeOperationGroup1?
                        new_sig_list->add(i->a, MusECore::TimeSignature(i->b, i->c), false);

                        updateFlags |= SC_SIG;
                        break;
                        

                  case UndoOp::DeleteKey:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:DeleteKey ** calling keymap.addOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_key_list)
                        {
                          new_key_list = new KeyList();
                          new_key_list->copy(MusEGlobal::keymap);
                        }
                        new_key_list->addKey(i->a, key_enum(i->b), i->c);

                        updateFlags |= SC_KEY;
                        break;

                  case UndoOp::AddKey:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:AddKey ** calling keymap.delOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_key_list)
                        {
                          new_key_list = new KeyList();
                          new_key_list->copy(MusEGlobal::keymap);
                        }
                        new_key_list->delKey(i->a);

                        updateFlags |= SC_KEY;
                        break;
                        
                  case UndoOp::ModifyKey:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:ModifyKey ** calling keymap.addOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_key_list)
                        {
                          new_key_list = new KeyList();
                          new_key_list->copy(MusEGlobal::keymap);
                        }
                        // TODO: Hm should that be ->d and ->e like in executeOperationGroup1?
                        new_key_list->addKey(i->a, key_enum(i->b), i->c);

                        updateFlags |= SC_KEY;
                        break;
                        
                  case UndoOp::ModifySongLen:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:ModifySongLen ** adding ModifySongLen operation\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(i->b, PendingOperationItem::ModifySongLength));
                        updateFlags |= SC_EVERYTHING;  // set all flags   // TODO Refine this! Too many flags.  // REMOVE Tim.
                        //updateFlags |= SC_SONG_LEN;
                        break;
                        
                  case UndoOp::ModifyMidiDivision:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::revertOperationGroup1:ModifyMidiDivision\n");
#endif                  
                        MusEGlobal::config.division = i->b;
                        // Make sure the AL namespace variable mirrors our variable.
                        AL::division = MusEGlobal::config.division;
                        // Defer normalize until end of stage 2.
                        updateFlags |= SC_DIVISION_CHANGED;
                        break;
                        
                  case UndoOp::AddMarker:
                          // Create the new list if it doesn't already exist.
                          // Make a copy of the original list.
                          if(!new_marker_list)
                            new_marker_list = new MarkerList(*marker());
                          if(i->newMarker)
                            new_marker_list->remove(*i->newMarker);
                          updateFlags |= SC_MARKER_REMOVED;
                        break;
                  
                  case UndoOp::DeleteMarker:
                          // Create the new list if it doesn't already exist.
                          // Make a copy of the original list.
                          if(!new_marker_list)
                            new_marker_list = new MarkerList(*marker());
                          if(i->oldMarker)
                            new_marker_list->add(*i->oldMarker);
                          updateFlags |= SC_MARKER_INSERTED;
                        break;
                  
                  case UndoOp::ModifyMarker:
                  case UndoOp::SetMarkerPos:
                          // Create the new list if it doesn't already exist.
                          // Make a copy of the original list.
                          if(!new_marker_list)
                            new_marker_list = new MarkerList(*marker());
                          if(i->newMarker)
                            new_marker_list->remove(*i->newMarker);
                          if(i->oldMarker)
                            new_marker_list->add(*i->oldMarker);
                          updateFlags |= SC_MARKER_MODIFIED;
                        break;

                  default:
                        break;
                  }
            }

      if(new_tempo_list)
        pendingOperations.add(PendingOperationItem(&MusEGlobal::tempomap, new_tempo_list, PendingOperationItem::ModifyTempoList));

      if(new_sig_list)
        pendingOperations.add(PendingOperationItem(&MusEGlobal::sigmap, new_sig_list, PendingOperationItem::ModifySigList));

      if(new_key_list)
        pendingOperations.add(PendingOperationItem(&MusEGlobal::keymap, new_key_list, PendingOperationItem::ModifyKeyList));

      if(new_marker_list)
        pendingOperations.add(PendingOperationItem(&_markerList, new_marker_list, PendingOperationItem::ModifyMarkerList));
      }

//---------------------------------------------------------
//   revertOperationGroup3
//    non realtime context
//---------------------------------------------------------

void Song::revertOperationGroup3(Undo& operations)
      {
      pendingOperations.executeNonRTStage();
#ifdef _UNDO_DEBUG_
      fprintf(stderr, "Song::revertOperationGroup3 *** Calling pendingOperations.clear()\n");
#endif      
      pendingOperations.clear();
      for (riUndoOp i = operations.rbegin(); i != operations.rend(); ++i) {
            Track* editable_track = const_cast<Track*>(i->track);
// uncomment if needed            Track* editable_property_track = const_cast<Track*>(i->_propertyTrack);
            Part* editable_part = const_cast<Part*>(i->part); // uncomment if needed
            switch(i->type) {
                  case UndoOp::AddTrack:
                        // Ensure that wave event sndfile file handles are closed.
                        // It should not be the job of the pending operations list to do this.
                        // TODO Coordinate close/open with part mute and/or track off.
                        editable_track->closeAllParts();
                        break;
                  case UndoOp::DeleteTrack:
                        switch(editable_track->type())
                        {
                          case Track::AUDIO_OUTPUT:
                            // Connect audio output ports to Jack ports...
                            if(MusEGlobal::checkAudioDevice() && MusEGlobal::audio->isRunning())
                            {
                              AudioOutput* ao = (AudioOutput*)editable_track;
                              for(int ch = 0; ch < ao->channels(); ++ch) 
                              {
                                void* our_port = ao->jackPort(ch);
                                if(!our_port)
                                  continue;
                                const char* our_port_name = MusEGlobal::audioDevice->canonicalPortName(our_port);
                                if(!our_port_name)
                                  continue;
                                RouteList* rl = ao->outRoutes();
                                for(ciRoute ir = rl->begin(); ir != rl->end(); ++ir) 
                                {
                                  if(ir->type != Route::JACK_ROUTE || ir->channel != ch)  
                                    continue;
                                  const char* route_name = ir->persistentJackPortName;
                                  //if(ir->jackPort)
                                  if(!MusEGlobal::audioDevice->findPort(route_name))
                                    continue;
                                  //if(!MusEGlobal::audioDevice->portConnectedTo(our_port, route_name))
                                  MusEGlobal::audioDevice->connect(our_port_name, route_name);
                                  updateFlags |= SC_ROUTE;
                                }
                              }
                            }
                          break;
                          
                          case Track::AUDIO_INPUT:
                            // Connect Jack ports to audio input ports...
                            if(MusEGlobal::checkAudioDevice() && MusEGlobal::audio->isRunning())
                            {
                              AudioInput* ai = (AudioInput*)editable_track;
                              for(int ch = 0; ch < ai->channels(); ++ch) 
                              {
                                void* our_port = ai->jackPort(ch);
                                if(!our_port)
                                  continue;
                                const char* our_port_name = MusEGlobal::audioDevice->canonicalPortName(our_port);
                                if(!our_port_name)
                                  continue;
                                RouteList* rl = ai->inRoutes();
                                for(ciRoute ir = rl->begin(); ir != rl->end(); ++ir) 
                                {
                                  if(ir->type != Route::JACK_ROUTE || ir->channel != ch)  
                                    continue;
                                  const char* route_name = ir->persistentJackPortName;
                                  //if(ir->jackPort)
                                  if(!MusEGlobal::audioDevice->findPort(route_name))
                                    continue;
                                  //if(!MusEGlobal::audioDevice->portConnectedTo(our_port, route_name))
                                  MusEGlobal::audioDevice->connect(route_name, our_port_name);
                                  updateFlags |= SC_ROUTE;
                                }
                              }
                            }
                          break;
                            
                          default:
                            break;
                        }
                        
                        break;
                  case UndoOp::AddPart:
                        // Ensure that wave event sndfile file handles are closed.
                        // It should not be the job of the pending operations list to do this.
                        // TODO Coordinate close/open with part mute and/or track off.
                        editable_part->closeAllEvents();
                        break;
                  case UndoOp::AddEvent: {
                        if(!i->nEvent.empty())
                        {
                          SndFileR f = i->nEvent.sndFile();
                          // Ensure that wave event sndfile file handle is closed.
                          // It should not be the job of the pending operations list to do this.
                          // TODO Coordinate close/open with part mute and/or track off.
                          if(!f.isNull() && f.isOpen())
                            f->close(); 
                        }
                        }
                        break;
                  case UndoOp::ModifyMidiDivision:
                        // This also tells all connected models to begin/end reset.
                        MusEGlobal::globalRasterizer->setDivision(i->b);
                        break;
                  default:
                        break;
                  }
            }
            
            if(!operations.empty())
              emit sigDirty();
      }

//---------------------------------------------------------
//   executeOperationGroup1
//    non realtime context
//    return true if nothing to do
//---------------------------------------------------------

void Song::executeOperationGroup1(Undo& operations)
      {
      unsigned song_len = len();
      MarkerList* new_marker_list = nullptr;
      TempoList* new_tempo_list = nullptr;
      SigList* new_sig_list = nullptr;
      KeyList* new_key_list = nullptr;
        
      for (iUndoOp i = operations.begin(); i != operations.end(); ++i) {
            Track* editable_track = const_cast<Track*>(i->track);
            Track* editable_property_track = const_cast<Track*>(i->_propertyTrack);
            Part* editable_part = const_cast<Part*>(i->part);
            switch(i->type) {
                  case UndoOp::SelectPart:
                        pendingOperations.add(PendingOperationItem(editable_part, i->selected, PendingOperationItem::SelectPart));
                        updateFlags |= SC_PART_SELECTION;
                        break;
                  case UndoOp::SelectEvent:
                        pendingOperations.add(PendingOperationItem(editable_part, i->nEvent, i->selected, PendingOperationItem::SelectEvent));
                        updateFlags |= SC_SELECTION;
                        break;
                        
                  case UndoOp::AddTrack:
                        switch(editable_track->type())
                        {
                          case Track::AUDIO_SOFTSYNTH:
                          {
                            SynthI* s = (SynthI*)editable_track;
                            Synth* sy = s->synth();
                            if(!s->isActivated()) 
                              s->initInstance(sy, s->name());
                          }
                          break;
                                
                          case Track::AUDIO_OUTPUT:
                          {
                            AudioOutput* ao = (AudioOutput*)editable_track;
                            if(MusEGlobal::checkAudioDevice())
                            {
                              for(int ch = 0; ch < ao->channels(); ++ch) 
                              {
                                // This should be OK since the track has not yet been added in the realtime stage.
                                if(ao->registerPorts(ch))
                                  updateFlags |= SC_ROUTE;
                                
                                // Set the route Jack ports now to relieve our graph callback handler from having to do it.
                                RouteList* rl = ao->outRoutes();
                                for(iRoute ir = rl->begin(); ir != rl->end(); ++ir)
                                  if(ir->type == Route::JACK_ROUTE && ir->channel == ch)
                                  {
                                    ir->jackPort = MusEGlobal::audioDevice->findPort(ir->persistentJackPortName);
                                    updateFlags |= SC_ROUTE;
                                  }
                              }
                            }
                            
                            
                          }
                          break;
                              
                          case Track::AUDIO_INPUT:
                          {
                            AudioInput* ai = (AudioInput*)editable_track;
                            if(MusEGlobal::checkAudioDevice())
                            {
                              for(int ch = 0; ch < ai->channels(); ++ch) 
                              {
                                // This should be OK since the track has not yet been added in the realtime stage.
                                if(ai->registerPorts(ch))
                                  updateFlags |= SC_ROUTE;
                                
                                // Set the route Jack ports now to relieve our graph callback handler from having to do it.
                                RouteList* rl = ai->inRoutes();
                                for(iRoute ir = rl->begin(); ir != rl->end(); ++ir)
                                  if(ir->type == Route::JACK_ROUTE && ir->channel == ch)
                                  {
                                    ir->jackPort = MusEGlobal::audioDevice->findPort(ir->persistentJackPortName);
                                    updateFlags |= SC_ROUTE;
                                  }
                              }
                            }
                          }
                          break;
                          
                          case Track::AUDIO_AUX:
                            updateFlags |= SC_AUX;
                          break;
                          
                          default:
                          break;
                        }
                        
                        // Ensure that wave event sndfile file handles are opened.
                        // It should not be the job of the pending operations list to do this.
                        // TODO Coordinate close/open with part mute and/or track off.
                        editable_track->openAllParts();

                        insertTrackOperation(editable_track, i->trackno, pendingOperations);
                        updateFlags |= SC_TRACK_INSERTED;
                        break;
                        
                  case UndoOp::DeleteTrack:
                        switch(editable_track->type())
                        {
                          case Track::AUDIO_SOFTSYNTH:
                          {
                            SynthI* si = (SynthI*)editable_track;
                            if(si->hasGui())
                              si->showGui(false);
                            if(si->hasNativeGui())       
                              si->showNativeGui(false);
                            // FIXME TODO: We want to clear any ports using this instrument AND make it
                            //  undoable but ATM a few other things can set the instrument without an undo
                            //  operation so the undo sequence would not be correct. So we don't have much
                            //  choice but to just reset inside the PendingOperation::DeleteTrack operation for now.
                            // Still, everything else is in place for undoable setting of instrument...
                          }// Fall through.
                          case Track::WAVE:
                          case Track::AUDIO_OUTPUT:
                          case Track::AUDIO_INPUT:
                          case Track::AUDIO_GROUP:
                          case Track::AUDIO_AUX:
                            ((AudioTrack*)editable_track)->deleteAllEfxGuis();
                            updateFlags |= SC_RACK;
                          break;
                          
                          default:
                          break;
                        }
                        
                        switch(editable_track->type())
                        {
                          case Track::AUDIO_OUTPUT:
                          {
                            AudioOutput* ao = (AudioOutput*)editable_track;
                            for(int ch = 0; ch < ao->channels(); ++ch)
                            {
                              MusEGlobal::audioDevice->unregisterPort(ao->jackPort(ch));
                              //ao->setJackPort(ch, 0);  // Done in RT stage.
                              updateFlags |= SC_ROUTE;
                            }
                          }
                          break;
                          
                          case Track::AUDIO_INPUT:
                          {
                            AudioOutput* ai = (AudioOutput*)editable_track;
                            for(int ch = 0; ch < ai->channels(); ++ch)
                            {
                              MusEGlobal::audioDevice->unregisterPort(ai->jackPort(ch));
                              //ai->setJackPort(ch, 0); // Done in RT stage.
                              updateFlags |= SC_ROUTE;
                            }
                          }     
                          break;
                          
                          case Track::AUDIO_AUX:
                            updateFlags |= SC_AUX;
                          break;
                          
                          default:
                          break;
                        }
                        removeTrackOperation(editable_track, pendingOperations);
                        updateFlags |= SC_TRACK_REMOVED;
                        break;
                        
                  case UndoOp::ModifyClip:
                        sndFileApplyUndoFile(i->nEvent, i->tmpwavfile, i->startframe, i->endframe);
                        updateFlags |= SC_CLIP_MODIFIED;
                        break;
                  case UndoOp::ModifyTrackChannel:
                        if (editable_property_track->isMidiTrack()) 
                        {
                          MusECore::MidiTrack* mt = static_cast<MusECore::MidiTrack*>(editable_property_track);
                          if (i->_newPropValue != mt->outChannel()) 
                          {
                                MusECore::MidiTrack::ChangedType_t changed = MusECore::MidiTrack::NothingChanged;
                                MusEGlobal::audio->msgIdle(true);
                                changed |= mt->setOutChanAndUpdate(i->_newPropValue, false);
                                MusEGlobal::audio->msgIdle(false);
                                MusEGlobal::audio->msgUpdateSoloStates();                   
                                updateFlags |= (SC_ROUTE | ((changed & MusECore::MidiTrack::DrumMapChanged) ? SC_DRUMMAP : 0));
                          }
                        }
                        else
                        {
                            if(editable_property_track->type() != MusECore::Track::AUDIO_SOFTSYNTH)
                            {
                              MusECore::AudioTrack* at = static_cast<MusECore::AudioTrack*>(editable_property_track);
                              if (i->_newPropValue != at->channels()) {
                                    MusEGlobal::audio->msgSetChannels(at, i->_newPropValue);
                                    updateFlags |= SC_CHANNELS;
                                    }
                            }         
                        }      
                        break;

                  case UndoOp::SetTrackRecord:
                        if(!editable_track->setRecordFlag1(i->a))
                          break;
                        pendingOperations.add(PendingOperationItem(editable_track, i->a, PendingOperationItem::SetTrackRecord));
                        // FIXME: No choice but to include monitor flag. Really should try to merge pending ops flags
                        //  with undo flags after executing the pending ops in executeOperationGroup3...
                        updateFlags |= (SC_RECFLAG | SC_TRACK_REC_MONITOR);
                        break;

                  case UndoOp::SetTrackMute:
                        pendingOperations.add(PendingOperationItem(editable_track, i->a, PendingOperationItem::SetTrackMute));
                        updateFlags |= SC_MUTE;
                        break;

                  case UndoOp::SetTrackSolo:
                        pendingOperations.add(PendingOperationItem(editable_track, i->a, PendingOperationItem::SetTrackSolo));
                        updateFlags |= SC_SOLO;
                        break;

                  case UndoOp::SetTrackRecMonitor:
                        pendingOperations.add(PendingOperationItem(editable_track, i->a, PendingOperationItem::SetTrackRecMonitor));
                        updateFlags |= SC_TRACK_REC_MONITOR;
                        break;

                  case UndoOp::SetTrackOff:
                        pendingOperations.add(PendingOperationItem(editable_track, i->a, PendingOperationItem::SetTrackOff));
                        updateFlags |= SC_MUTE;
                        break;

                        
                  case UndoOp::AddRoute:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:AddRoute\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(i->routeFrom, i->routeTo, PendingOperationItem::AddRoute)); 
                        updateFlags |= SC_ROUTE;
                        break;
                        
                  case UndoOp::DeleteRoute:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:DeleteRoute\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(i->routeFrom, i->routeTo, PendingOperationItem::DeleteRoute)); 
                        updateFlags |= SC_ROUTE;
                        break;
                        
                  case UndoOp::ModifyTrackName:
                        pendingOperations.add(PendingOperationItem(editable_track, i->_newName, PendingOperationItem::ModifyTrackName));
                        updateFlags |= (SC_TRACK_MODIFIED | SC_MIDI_TRACK_PROP);
                        // If it's an aux track, notify aux UI controls to reload, or change their names etc.
                        if(editable_track->type() == Track::AUDIO_AUX)
                          updateFlags |= SC_AUX;
                        break;
                        
                  case UndoOp::MoveTrack:
                        pendingOperations.add(PendingOperationItem(&_tracks, i->a, i->b, PendingOperationItem::MoveTrack));
                        updateFlags |= SC_TRACK_MOVED;
                        break;
                        
                  case UndoOp::ModifyPartName:
                        pendingOperations.add(PendingOperationItem(editable_part, i->_newName, PendingOperationItem::ModifyPartName));
                        updateFlags |= SC_PART_MODIFIED;
                        break;

                  case UndoOp::ModifyPartStart:
                      {
                        pendingOperations.modifyPartStartOperation(
                          editable_part, i->new_partlen_or_pos, i->new_partlen, i->events_offset, i->events_offset_time_type);
                        updateFlags |= SC_PART_MODIFIED;
                        // If the part had events, then treat it as if they were added/removed with separate Add/DeleteEvent operations.
                        // Even if they will be added/deleted later in this operations group with actual separate Add/DeleteEvent operations,
                        //  that's an SC_EVENT_ADDED/REMOVED anyway, so hopefully no harm.
                        if(i->events_offset != 0 && !editable_part->events().empty())
                          updateFlags |= (SC_EVENT_INSERTED | SC_EVENT_REMOVED | SC_EVENT_MODIFIED);
                      }
                      break;
                  case UndoOp::ModifyPartLength: 
                        {
                          unsigned p = Pos::convert(editable_part->posValue() + i->new_partlen_or_pos, editable_part->type(), Pos::TICKS);
                          // >= for good luck, since this (simpler) comparison is in the TICKS domain.
                          if(p >= song_len)
                          {
                            song_len = p + 1; 
                            // Insert a ModifySongLen operation BEFORE this one. If insert finds an existing ModifySongLen,
                            //  possibly long before this one, it REPLACES that one's values.
                            operations.insert(i, UndoOp(UndoOp::ModifySongLen, song_len, len()));
                            // Since the ModifySongLen above will not be iterated now, act like the operation had just been iterated. 
                            // The same REPLACEMENT rules apply here.
                            pendingOperations.add(PendingOperationItem(song_len, PendingOperationItem::ModifySongLength));
                            updateFlags |= SC_EVERYTHING;  // set all flags   // TODO Refine this! Too many flags.  // REMOVE Tim.
                            //updateFlags |= SC_SONG_LEN;
                          }
                          pendingOperations.modifyPartLengthOperation(
                            editable_part, i->new_partlen_or_pos, i->events_offset, i->events_offset_time_type);
                          updateFlags |= SC_PART_MODIFIED;
                          // If the part had events, then treat it as if they were added/removed with separate Add/DeleteEvent operations.
                          // Even if they will be added/deleted later in this operations group with actual separate Add/DeleteEvent operations,
                          //  that's an SC_EVENT_ADDED/REMOVED anyway, so hopefully no harm.
                          if(i->events_offset != 0 && !editable_part->events().empty())
                            updateFlags |= (SC_EVENT_INSERTED | SC_EVENT_REMOVED | SC_EVENT_MODIFIED);
                        }
                        break;
                        
                  case UndoOp::MovePart:
                        {
                          unsigned p = Pos::convert(editable_part->lenValue() + i->new_partlen_or_pos, editable_part->type(), Pos::TICKS);
                          // >= for good luck, since this (simpler) comparison is in the TICKS domain.
                          if(p >= song_len)
                          {
                            song_len = p + 1; 
                            // Insert a ModifySongLen operation BEFORE this one. If insert finds an existing ModifySongLen,
                            //  possibly long before this one, it REPLACES that one's values.
                            operations.insert(i, UndoOp(UndoOp::ModifySongLen, song_len, len()));
                            // Since the ModifySongLen above will not be iterated now, act like the operation had just been iterated. 
                            // The same REPLACEMENT rules apply here.
                            pendingOperations.add(PendingOperationItem(song_len, PendingOperationItem::ModifySongLength));
                            updateFlags |= SC_EVERYTHING;  // set all flags   // TODO Refine this! Too many flags.  // REMOVE Tim.
                            //updateFlags |= SC_SONG_LEN;
                          }
#ifdef _UNDO_DEBUG_
                          fprintf(stderr, "Song::executeOperationGroup1:MovePart ** calling parts->movePartOperation\n");
#endif                        
                          pendingOperations.movePartOperation(editable_part->track()->parts(),
                            editable_part, i->new_partlen_or_pos, editable_track);
                          if(editable_track)
                            updateFlags |= SC_PART_INSERTED | SC_PART_REMOVED;
                          updateFlags |= SC_PART_MODIFIED;
                        }
                        break;
                        
                  case UndoOp::AddPart:
                        {
                          unsigned p = Pos::convert(editable_part->lenValue() + editable_part->posValue(), editable_part->type(), Pos::TICKS);
                          // >= for good luck, since this (simpler) comparison is in the TICKS domain.
                          if(p >= song_len)
                          {
                            song_len = p + 1; 
                            // Insert a ModifySongLen operation BEFORE this one. If insert finds an existing ModifySongLen,
                            //  possibly long before this one, it REPLACES that one's values.
                            operations.insert(i, UndoOp(UndoOp::ModifySongLen, song_len, len()));
                            // Since the ModifySongLen above will not be iterated now, act like the operation had just been iterated. 
                            // The same REPLACEMENT rules apply here.
                            pendingOperations.add(PendingOperationItem(song_len, PendingOperationItem::ModifySongLength));
                            updateFlags |= SC_EVERYTHING;  // set all flags   // TODO Refine this! Too many flags.  // REMOVE Tim.
                            //updateFlags |= SC_SONG_LEN;
                          }
#ifdef _UNDO_DEBUG_
                          fprintf(stderr, "Song::executeOperationGroup1:addPart ** calling parts->addOperation\n");
#endif                        
                          // Ensure that wave event sndfile file handles are opened.
                          // It should not be the job of the pending operations list to do this.
                          // TODO Coordinate close/open with part mute and/or track off.
                          editable_part->openAllEvents();
                          
                          pendingOperations.addPartOperation(editable_part->track()->parts(), editable_part);
                          updateFlags |= SC_PART_INSERTED;
                          // If the part has events, then treat it as if they were inserted with separate AddEvent operations.
                          // Even if some will be inserted later in this operations group with actual separate AddEvent operations,
                          //  that's an SC_EVENT_INSERTED anyway, so should be no harm.
                          if(!editable_part->events().empty())
                            updateFlags |= SC_EVENT_INSERTED;
                        }
                        break;
                    
                  case UndoOp::DeletePart:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:deletePart ** calling parts->delOperation\n");
#endif                        
                        pendingOperations.delPartOperation(editable_part->track()->parts(), editable_part);
                        updateFlags |= SC_PART_REMOVED;
                        // If the part had events, then treat it as if they were removed with separate DeleteEvent operations.
                        // Even if they will be deleted later in this operations group with actual separate DeleteEvent operations,
                        //  that's an SC_EVENT_REMOVED anyway, so hopefully no harm. This fixes a problem with midi controller canvas
                        //  not updating after such a 'delete part with events, no separate AddEvents were used when creating the part'.
                        if(!editable_part->events().empty())
                          updateFlags |= SC_EVENT_REMOVED;
                        break;
                    
                  case UndoOp::AddEvent: {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:AddEvent ** calling addEvent\n");
#endif                        
                        if(!i->nEvent.empty())
                        {
                          SndFileR f = i->nEvent.sndFile();
                          // Ensure that wave event sndfile file handle is opened.
                          // It should not be the job of the pending operations list to do this.
                          // TODO Coordinate close/open with part mute and/or track off.
                          if(!f.isNull() && !f.isOpen())
                            f.openRead();
                        }
                        
                        addEventOperation(i->nEvent, editable_part, i->doCtrls, i->doClones);
                        updateFlags |= SC_EVENT_INSERTED;
                        }
                        break;
                        
                  case UndoOp::DeleteEvent: {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:DeleteEvent ** calling deleteEvent\n");
#endif                        
                        // Special: Replace the undo item's event with the real actual event found in the event lists.
                        // This way even a modified event can be passed in to the DeleteEvent operation constructor,
                        //  and as long as the ID AND position values match it will find and use the ORIGINAL event.
                        // (It's safe, the = operator quickly returns if the two events have the same base pointer.)
                        i->nEvent = deleteEventOperation(i->nEvent, editable_part, i->doCtrls, i->doClones);
                        updateFlags |= SC_EVENT_REMOVED;
                        }
                        break;
                        
                  case UndoOp::ModifyEvent:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:ModifyEvent ** calling changeEvent\n");
#endif                        
                        // Special: Replace the undo item's old event with the real actual event found in the event lists.
                        // This way even a modified old event can be passed in to the ModifyEvent operation constructor,
                        //  and as long as the ID AND position values match it will find and use the ORIGINAL event.
                        // (It's safe, the = operator quickly returns if the two events have the same base pointer.)
                        i->oEvent = changeEventOperation(i->oEvent, i->nEvent, editable_part, i->doCtrls, i->doClones);
                        updateFlags |= SC_EVENT_MODIFIED;
                        break;

                        
                  case UndoOp::AddAudioCtrlVal:
                  {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:AddAudioCtrlVal\n");
#endif                        
                        CtrlListList* cll = static_cast<AudioTrack*>(editable_track)->controller();
                        iCtrlList icl = cll->find(i->_audioCtrlID);
                        if(icl != cll->end())
                        {
                          //CtrlList* cl = icl->second;
                          //iCtrl ic = cl->find(i->_audioCtrlFrame);
                          //if(ic != cl->end())
                          //  // An existing value was found. Replace it with the new value.
                          //  pendingOperations.add(PendingOperationItem(icl->second, ic, i->_audioCtrlVal, PendingOperationItem::ModifyAudioCtrlVal));
                          //else
                            // Add the new value.
                            pendingOperations.add(PendingOperationItem(icl->second, i->_audioCtrlFrame, i->_audioCtrlVal, PendingOperationItem::AddAudioCtrlVal));
                          updateFlags |= SC_AUDIO_CONTROLLER;
                        }
                  }
                  break;

                  case UndoOp::DeleteAudioCtrlVal:
                  {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:DeleteAudioCtrlVal\n");
#endif                        
                        CtrlListList* cll = static_cast<AudioTrack*>(editable_track)->controller();
                        iCtrlList icl = cll->find(i->_audioCtrlID);
                        if(icl != cll->end())
                        {
                          CtrlList* cl = icl->second;
                          iCtrl ic = cl->find(i->_audioCtrlFrame);
                          if(ic != cl->end())
                          {
                            i->_audioCtrlVal = ic->second.val; // Store the existing value so it can be restored.
                            pendingOperations.add(PendingOperationItem(cl, ic, PendingOperationItem::DeleteAudioCtrlVal));
                            updateFlags |= SC_AUDIO_CONTROLLER;
                          }
                        }
                  }
                  break;
                        
                  case UndoOp::ModifyAudioCtrlVal:
                  {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:ModifyAudioCtrlVal\n");
#endif                        
                        CtrlListList* cll = static_cast<AudioTrack*>(editable_track)->controller();
                        iCtrlList icl = cll->find(i->_audioCtrlID);
                        if(icl != cll->end())
                        {
                          CtrlList* cl = icl->second;
                          iCtrl ic = cl->find(i->_audioCtrlFrame);
                          if(ic != cl->end())
                          {
                            i->_audioCtrlVal = ic->second.val; // Store the existing value so it can be restored.
                            pendingOperations.add(PendingOperationItem(icl->second, ic, i->_audioNewCtrlFrame, i->_audioNewCtrlVal, PendingOperationItem::ModifyAudioCtrlVal));
                            updateFlags |= SC_AUDIO_CONTROLLER;
                          }
                        }
                  }
                  break;
                        
                  case UndoOp::ModifyAudioCtrlValList:
                  {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:ModifyAudioCtrlValList\n");
#endif                        
                        // Take either id. At least one list must be valid.
                        const int id = i->_eraseCtrlList ? i->_eraseCtrlList->id() : i->_addCtrlList->id();
                        iCtrlList icl = i->_ctrlListList->find(id);
                        if(icl != i->_ctrlListList->end())
                        {
                          // Make a complete copy of the controller list. The list will be quickly switched in the realtime stage.
                          // The Pending Operations system will take 'ownership' of this and delete it at the appropriate time.
                          CtrlList* new_list = new CtrlList(*icl->second, CtrlList::ASSIGN_PROPERTIES | CtrlList::ASSIGN_VALUES);
                          
                          if(i->_eraseCtrlList && !i->_eraseCtrlList->empty())
                          {
                              iCtrl n_s = new_list->find(i->_eraseCtrlList->begin()->second.frame); // The first item to be erased.
                              ciCtrl e_e = i->_eraseCtrlList->end();
                              --e_e;
                              iCtrl n_e = new_list->find(e_e->second.frame); // The last item to be erased.
                              if(n_s != new_list->end() && n_e != new_list->end())
                              {
                                // Since std range does NOT include the last iterator, increment n_e so erase will get all items.
                                ++n_e;
                                new_list->erase(n_s, n_e);
                              }
                          }
                          
                          // Add any items in the add list...
                          if(i->_addCtrlList && !i->_addCtrlList->empty())
                            new_list->insert(i->_addCtrlList->begin(), i->_addCtrlList->end());
                          
                          // The operation will quickly switch the list in the RT stage then the delete the old list in the non-RT stage.
                          pendingOperations.add(PendingOperationItem(icl, new_list, PendingOperationItem::ModifyAudioCtrlValList));
                          updateFlags |= SC_AUDIO_CONTROLLER_LIST;
                        }
                  }
                  break;
                        
                        
                  case UndoOp::SetInstrument:
                  {
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:SetInstrument\n");
#endif                        
                        // Set the new value.
                        pendingOperations.add(PendingOperationItem(
                          i->_midiPort, i->_newMidiInstrument,
                          PendingOperationItem::SetInstrument));
                        updateFlags |= SC_MIDI_INSTRUMENT;
                  }
                  break;


                  case UndoOp::AddTempo:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:AddTempo ** calling tempomap.addOperation tick:%d tempo:%d\n", i->a, i->b);
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_tempo_list)
                        {
                          new_tempo_list = new TempoList();
                          new_tempo_list->copy(MusEGlobal::tempomap);
                        }
                        // Defer normalize until end of stage 2.
                        new_tempo_list->addTempo(i->a, i->b, false);

                        updateFlags |= SC_TEMPO;
                        break;
                        
                  case UndoOp::DeleteTempo:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:DeleteTempo ** calling tempomap.delOperation tick:%d\n", i->a);
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_tempo_list)
                        {
                          new_tempo_list = new TempoList();
                          new_tempo_list->copy(MusEGlobal::tempomap);
                        }
                        // Defer normalize until end of stage 2.
                        new_tempo_list->delTempo(i->a, false);

                        updateFlags |= SC_TEMPO;
                        break;
                        
                  case UndoOp::ModifyTempo:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:ModifyTempo ** calling tempomap.addOperation tick:%d tempo:%d\n", i->a, i->c);
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_tempo_list)
                        {
                          new_tempo_list = new TempoList();
                          new_tempo_list->copy(MusEGlobal::tempomap);
                        }
                        // Defer normalize until end of stage 2.
                        new_tempo_list->addTempo(i->a, i->c, false);

                        updateFlags |= SC_TEMPO;
                        break;

//                   case UndoOp::SetTempo:
//                         // Only if the master is on.
//                         if(MusEGlobal::tempomap.masterFlag())
//                         {
// #ifdef _UNDO_DEBUG_
//                           fprintf(stderr, "Song::executeOperationGroup1:SetTempo ** calling tempomap.addOperation tick:%d tempo:%d\n", i->a, i->b);
// #endif                        
//                           MusEGlobal::tempomap.addOperation(i->a, i->b, pendingOperations);
//                         }
//                         else
//                           pendingOperations.add(PendingOperationItem(&MusEGlobal::tempomap, i->a, PendingOperationItem::SetStaticTempo));
//                         updateFlags |= SC_TEMPO;
//                         break;
                        
                  case UndoOp::SetStaticTempo:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:SetStaticTempo ** adding SetStaticTempo operation\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(&MusEGlobal::tempomap, i->a, PendingOperationItem::SetStaticTempo));
                        updateFlags |= SC_TEMPO;
                        break;
                        
                  case UndoOp::SetGlobalTempo:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:SetGlobalTempo ** adding SetGlobalTempo operation\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(&MusEGlobal::tempomap, i->a, PendingOperationItem::SetGlobalTempo));
                        updateFlags |= SC_TEMPO;
                        break;
                        
                  case UndoOp::EnableMasterTrack:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:EnableMasterTrack ** adding EnableMasterTrack operation\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(&MusEGlobal::tempomap, (bool)i->a, PendingOperationItem::SetUseMasterTrack));
                        updateFlags |= SC_MASTER;
                        break;
                        
                  case UndoOp::AddSig:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:AddSig ** calling MusEGlobal::sigmap.addOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_sig_list)
                        {
                          new_sig_list = new SigList();
                          new_sig_list->copy(MusEGlobal::sigmap);
                        }
                        // Defer normalize until end of stage 2.
                        new_sig_list->add(i->a, MusECore::TimeSignature(i->b, i->c), false);

                        updateFlags |= SC_SIG;
                        break;
                        
                  case UndoOp::DeleteSig:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:DeleteSig ** calling MusEGlobal::sigmap.delOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_sig_list)
                        {
                          new_sig_list = new SigList();
                          new_sig_list->copy(MusEGlobal::sigmap);
                        }
                        // Defer normalize until end of stage 2.
                        new_sig_list->del(i->a, false);

                        updateFlags |= SC_SIG;
                        break;
                        
                  case UndoOp::ModifySig:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:ModifySig ** calling MusEGlobal::sigmap.addOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_sig_list)
                        {
                          new_sig_list = new SigList();
                          new_sig_list->copy(MusEGlobal::sigmap);
                        }
                        // Defer normalize until end of stage 2.
                        new_sig_list->add(i->a, MusECore::TimeSignature(i->d, i->e), false);

                        updateFlags |= SC_SIG;
                        break;

                        
                  case UndoOp::AddKey:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:AddKey ** calling keymap.addOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_key_list)
                        {
                          new_key_list = new KeyList();
                          new_key_list->copy(MusEGlobal::keymap);
                        }
                        new_key_list->addKey(i->a, key_enum(i->b), i->c);

                        updateFlags |= SC_KEY;
                        break;
                        
                  case UndoOp::DeleteKey:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:DeleteKey ** calling keymap.delOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_key_list)
                        {
                          new_key_list = new KeyList();
                          new_key_list->copy(MusEGlobal::keymap);
                        }
                        new_key_list->delKey(i->a);

                        updateFlags |= SC_KEY;
                        break;
                        
                  case UndoOp::ModifyKey:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:ModifyKey ** calling keymap.addOperation\n");
#endif                        
                        // Create the new list if it doesn't already exist.
                        // Make a copy of the original list.
                        if(!new_key_list)
                        {
                          new_key_list = new KeyList();
                          new_key_list->copy(MusEGlobal::keymap);
                        }
                        new_key_list->addKey(i->a, key_enum(i->d), i->e);

                        updateFlags |= SC_KEY;
                        break;

                  case UndoOp::ModifySongLen:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:ModifySongLen ** adding ModifySongLen operation\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(i->a, PendingOperationItem::ModifySongLength));
                        updateFlags |= SC_EVERYTHING;  // set all flags   // TODO Refine this! Too many flags.  // REMOVE Tim.
                        //updateFlags |= SC_SONG_LEN;
                        break;
                        
                  case UndoOp::ModifyMidiDivision:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:ModifyMidiDivision\n");
#endif                  
                        MusEGlobal::config.division = i->a;
                        // Make sure the AL namespace variable mirrors our variable.
                        AL::division = MusEGlobal::config.division;
                        // Defer normalize until end of stage 2.
                        updateFlags |= SC_DIVISION_CHANGED;
                        break;
                        
                  case UndoOp::EnableAllAudioControllers:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:EnableAllAudioControllers\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(PendingOperationItem::EnableAllAudioControllers));
                        updateFlags |= SC_AUDIO_CONTROLLER;
                        break;
                        
                  case UndoOp::NormalizeMidiDivision:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:NormalizeMidiDivision\n");
#endif                  
                        // Nothing to do here.
                        // Defer normalize until end of stage 2.
                        updateFlags |= SC_DIVISION_CHANGED;
                        break;
                        
                  case UndoOp::GlobalSelectAllEvents:
#ifdef _UNDO_DEBUG_
                        fprintf(stderr, "Song::executeOperationGroup1:GlobalSelectAllEvents\n");
#endif                        
                        pendingOperations.add(PendingOperationItem(tracks(), i->a, 0, 0, PendingOperationItem::GlobalSelectAllEvents));
                        updateFlags |= SC_SELECTION;
                        break;
                        
                  case UndoOp::AddMarker:
                          // Create the new list if it doesn't already exist.
                          // Make a copy of the original list.
                          if(!new_marker_list)
                            new_marker_list = new MarkerList(*marker());
                          if(i->newMarker)
                            new_marker_list->add(*i->newMarker);
                          updateFlags |= SC_MARKER_INSERTED;
                        break;
                  
                  case UndoOp::DeleteMarker:
                          // Create the new list if it doesn't already exist.
                          // Make a copy of the original list.
                          if(!new_marker_list)
                            new_marker_list = new MarkerList(*marker());
                          if(i->oldMarker)
                            new_marker_list->remove(*i->oldMarker);
                          updateFlags |= SC_MARKER_REMOVED;
                        break;
                  
                  case UndoOp::ModifyMarker:
                  case UndoOp::SetMarkerPos:
                          // Create the new list if it doesn't already exist.
                          // Make a copy of the original list.
                          if(!new_marker_list)
                            new_marker_list = new MarkerList(*marker());
                          if(i->oldMarker)
                            new_marker_list->remove(*i->oldMarker);
                          if(i->newMarker)
                            new_marker_list->add(*i->newMarker);
                          updateFlags |= SC_MARKER_MODIFIED;
                        break;

                  default:
                        break;
                  }
            }

      if(new_tempo_list)
        pendingOperations.add(PendingOperationItem(&MusEGlobal::tempomap, new_tempo_list, PendingOperationItem::ModifyTempoList));

      if(new_sig_list)
        pendingOperations.add(PendingOperationItem(&MusEGlobal::sigmap, new_sig_list, PendingOperationItem::ModifySigList));

      if(new_key_list)
        pendingOperations.add(PendingOperationItem(&MusEGlobal::keymap, new_key_list, PendingOperationItem::ModifyKeyList));

      if(new_marker_list)
        pendingOperations.add(PendingOperationItem(&_markerList, new_marker_list, PendingOperationItem::ModifyMarkerList));
      }

//---------------------------------------------------------
//   executeOperationGroup3
//    non realtime context
//---------------------------------------------------------

void Song::executeOperationGroup3(Undo& operations)
      {
      pendingOperations.executeNonRTStage();
#ifdef _UNDO_DEBUG_
      fprintf(stderr, "Song::executeOperationGroup3 *** Calling pendingOperations.clear()\n");
#endif                        
      pendingOperations.clear();
      //bool song_has_changed = !operations.empty();
      for (iUndoOp i = operations.begin(); i != operations.end(); ) {
            Track* editable_track = const_cast<Track*>(i->track);
// uncomment if needed            Track* editable_property_track = const_cast<Track*>(i->_propertyTrack);
            Part* editable_part = const_cast<Part*>(i->part); // uncomment if needed
            switch(i->type) {
                  case UndoOp::AddTrack:
                        switch(editable_track->type())
                        {
                          case Track::AUDIO_OUTPUT:
                            // Connect audio output ports to Jack ports...
                            if(MusEGlobal::checkAudioDevice() && MusEGlobal::audio->isRunning())
                            {
                              AudioOutput* ao = (AudioOutput*)editable_track;
                              for(int ch = 0; ch < ao->channels(); ++ch) 
                              {
                                void* our_port = ao->jackPort(ch);
                                if(!our_port)
                                  continue;
                                const char* our_port_name = MusEGlobal::audioDevice->canonicalPortName(our_port);
                                if(!our_port_name)
                                  continue;
                                RouteList* rl = ao->outRoutes();
                                for(ciRoute ir = rl->begin(); ir != rl->end(); ++ir) 
                                {
                                  if(ir->type != Route::JACK_ROUTE || ir->channel != ch)  
                                    continue;
                                  const char* route_name = ir->persistentJackPortName;
                                  //if(ir->jackPort)
                                  if(!MusEGlobal::audioDevice->findPort(route_name))
                                    continue;
                                  //if(!MusEGlobal::audioDevice->portConnectedTo(our_port, route_name))
                                  MusEGlobal::audioDevice->connect(our_port_name, route_name);
                                  updateFlags |= SC_ROUTE;
                                }
                              }
                            }
                          break;
                          
                          case Track::AUDIO_INPUT:
                            // Connect Jack ports to audio input ports...
                            if(MusEGlobal::checkAudioDevice() && MusEGlobal::audio->isRunning())
                            {
                              AudioInput* ai = (AudioInput*)editable_track;
                              for(int ch = 0; ch < ai->channels(); ++ch) 
                              {
                                void* our_port = ai->jackPort(ch);
                                if(!our_port)
                                  continue;
                                const char* our_port_name = MusEGlobal::audioDevice->canonicalPortName(our_port);
                                if(!our_port_name)
                                  continue;
                                RouteList* rl = ai->inRoutes();
                                for(ciRoute ir = rl->begin(); ir != rl->end(); ++ir) 
                                {
                                  if(ir->type != Route::JACK_ROUTE || ir->channel != ch)  
                                    continue;
                                  const char* route_name = ir->persistentJackPortName;
                                  //if(ir->jackPort)
                                  if(!MusEGlobal::audioDevice->findPort(route_name))
                                    continue;
                                  //if(!MusEGlobal::audioDevice->portConnectedTo(our_port, route_name))
                                  MusEGlobal::audioDevice->connect(route_name, our_port_name);
                                  updateFlags |= SC_ROUTE;
                                }
                              }
                            }
                          break;
                            
                          default:
                            break;
                        }
                        
                        break;
                  case UndoOp::DeleteTrack:
                        // Ensure that wave event sndfile file handles are closed.
                        // It should not be the job of the pending operations list to do this.
                        // TODO Coordinate close/open with part mute and/or track off.
                        editable_track->closeAllParts();
                        break;
                  case UndoOp::DeletePart:
                        // Ensure that wave event sndfile file handles are closed.
                        // It should not be the job of the pending operations list to do this.
                        // TODO Coordinate close/open with part mute and/or track off.
                        editable_part->closeAllEvents();
                        break;
                  case UndoOp::DeleteEvent: {
                          if(!i->nEvent.empty())
                          {
                            SndFileR f = i->nEvent.sndFile();
                            // Ensure that wave event sndfile file handle is closed.
                            // It should not be the job of the pending operations list to do this.
                            // TODO Coordinate close/open with part mute and/or track off.
                            if(!f.isNull() && f.isOpen())
                              f.close();
                          }
                        }
                        break;
                  case UndoOp::ModifyMidiDivision:
                        // This also tells all connected models to begin/end reset.
                        MusEGlobal::globalRasterizer->setDivision(i->a);
                        break;
                   default:
                        break;
                  }
            
            // Is the operation marked as non-undoable? Remove it from the list.
            if(i->_noUndo)
              i = operations.erase(i);
            else
              ++i;
            }
            
      // If some operations marked as non-undoable were removed, it is OK,
      //  because we only want dirty if an undoable operation was executed, right?
      if(!operations.empty())
      // Hm, no. ANY operation actually changes things, so yes, the song is dirty.
      //if(song_has_changed)
        emit sigDirty();
      }


bool Undo::empty() const
{
  if (std::list<UndoOp>::empty()) return true;
  
  for (const_iterator it=begin(); it!=end(); it++)
    if (it->type!=UndoOp::DoNothing)
      return false;
  
  return true;
}

} // namespace MusECore
