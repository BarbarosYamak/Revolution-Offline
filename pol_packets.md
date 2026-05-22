Title: POL Packet's Guide - Saved on 2010-01-09

URL Source: https://github.com/necr0potenc3/POLPacketGuide/raw/refs/heads/master/index.html

Markdown Content:
---
description: Penultima Online Documentation website.
---

#  POL Packet's Guide - Saved on 2010-01-09 

 Offline versions are outdated as soon as they're saved, keep that in mind. Check online version for changes.

# Description

[\[^\]](#TOP)

**Packet Name:** Follow  
Last Modified: 2009-08-13 08:58:30  
Modified By: MuadDib  
  
**Packet: 0x15** 
Sent By: Both  
Size: 9 bytes  

  
**Packet Build**  
BYTE\[1\] 0x15  
BYTE\[4\] Serial of "To Follow"  
BYTE\[4\] Serial of "Is Following"  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Character Move ACK/ Resync Request  
Last Modified: 2009-08-13 08:59:28  
Modified By: MuadDib  
  
**Packet: 0x22** 
Sent By: Both  
Size: 3 bytes  

  
**Packet Build**  
BYTE\[1\] 0x22  
BYTE\[1\] Movement Sequence Key  
BYTE\[1\] Notoriety Flag  

  
**Subcommand Build**  
N/A

  
**Notes**  
Notoriety  
0 = invalid/across server line  
1 = innocent (blue)  
2 = guilded/ally (green)  
3 = attackable but not criminal (gray)  
4 = criminal (gray)  
5 = enemy (orange)  
6 = murderer (red)  
7 = unknown use (translucent (like 0x4000 hue))  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Resurrection Menu  
Last Modified: 2009-08-13 09:01:35  
Modified By: MuadDib  
  
**Packet: 0x2C** 
Sent By: Both  
Size: 2 bytes  

  
**Packet Build**  
BYTE\[1\] 0x2C  
BYTE\[1\] Action  

  
**Subcommand Build**  
N/A

  
**Notes**  
The only use on OSI for this packet is now sending "2C02" for the "You Are Dead" screen upon character death.  
  
Action  
0: Server sent  
1: Resurrect  
2: Ghost  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Remove (Group)  
Last Modified: 2009-02-17 20:21:53  
Modified By: MuadDib  
  
**Packet: 0x39** 
Sent By: Both  
Size: 9 bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[4\] Player serial  
BYTE\[4\] Target serial  

  
**Subcommand Build**  
N/A

  
**Notes**  
Outdated packet, no longer seen to be in use on OSI recently.  
  
If player serial is zeroed, then it removes the target serial from a group?  
  
If player serial and target serial are the same, it reports removed from group to the client.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Send Skills  
Last Modified: 2009-03-02 05:23:55  
Modified By: Turley  
  
**Packet: 0x3A** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
Server Version:  
BYTE\[1\] Command  
BYTE\[2\] Length  
BYTE\[1\] Type  
**Repeat next for each skill**  
* BYTE\[2\] Skill ID Number
* BYTE\[2\] Skill Value \* 10
* BYTE\[2\] Unmodified Value \* 10
* BYTE\[1\] Skill Lock
**If (Type==2 || Type==0xDF)**  
* BYTE\[2\] Skill Cap
  
BYTE\[2\] Null Terminated(0000) (ONLY IF TYPE == 0x00)  
  
Client Version (sets skill lock states):  
BYTE\[1\] Command  
BYTE\[2\] Length  
BYTE\[2\] Skill ID Number  
BYTE\[1\] Skill Lock State  

  
**Subcommand Build**  
N/A

  
**Notes**  
Type  
  
0x00: Full List of skills  
0xFF: Single skill update  
0x02: Full List of skills with skill cap for each skill  
0xDF: Single skill update with skill cap for skill  
  
Skill Lock State  
0x0: Up  
0x1: Down  
0x2: Locked  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Map Packet (cartography/treasure)  
Last Modified: 2009-08-13 09:14:36  
Modified By: MuadDib  
  
**Packet: 0x56** 
Sent By: Both  
Size: 11 Bytes  

  
**Packet Build**  
BYTE\[1\] 0x56  
BYTE\[4\] Map Object Serial  
BYTE\[1\] Action Flag (see notes)  
BYTE\[1\] Pin ID or Plotting Flag (see notes)  
BYTE\[2\] X  
BYTE\[2\] Y  

  
**Subcommand Build**  
N/A

  
**Notes**  
Action Flag  
* 1: Add Pin
* 2: Insert New Pin
* 3: Change Pin
* 4: Remove Pin
* 5: Clear Pins
* 6: Toggle Edit Map (Client)
* 7: Reply From Server to Action 6
  
Plotting Flag: Server Sent With Action Flag 7 Only  
* 1: Plotting On
* 0: Plotting Off
  
X and Y location are relative to upper left corner of the map, in pixels  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Books (Pages)  
Last Modified: 2010-01-08 02:04:47  
Modified By: Tomi  
  
**Packet: 0x66** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x66  
BYTE\[2\] Length  
BYTE\[4\] Book Serial  
BYTE\[2\] Page Count  
Page Loop  
* BYTE\[2\] Page #
* BYTE\[2\] Line Count
* Line Loop ( if used old packet 0x93 to open the book )
* * BYTE\[var\] Line Text, Null Terminated
* Line Loop ( if used new packet 0xD4 to open the book )
* * BYTE\[var\]\[2\] Unicode Line Text, Null Terminated

  
**Subcommand Build**  
N/A

  
**Notes**  
Server Version  
\# of pages equals value given in 0x93/0xd4  
EACH page # given. If empty: # lines: 0 + terminator (=3 0's)  
  
  
Client Version  
\# of pages always 1\. if 2 pages changed, client generates 2 packets.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Target Cursor Commands  
Last Modified: 2009-08-15 08:58:16  
Modified By: MuadDib  
  
**Packet: 0x6C** 
Sent By: Both  
Size: 19 Bytes  

  
**Packet Build**  
BYTE\[1\] 0x6C  
BYTE\[1\] Cursor Target (see notes)  
BYTE\[4\] Cursor ID  
BYTE\[1\] Cursor Type (see notes)  
The following are always sent but are only valid if sent by client   
* BYTE\[4\] Clicked On ID
* BYTE\[2\] X
* BYTE\[2\] Y
* BYTE\[1\] unknown (0x00)
* BYTE\[1\] Z
* BYTE\[2\] Graphic (if a static tile, 0 if a map/landscape tile)

  
**Subcommand Build**  
N/A

  
**Notes**  
Cursor Target  
* 0: Select Object
* 1: Select X, Y, Z
  
Cursor Type  
* 0: Neutral
* 1: Harmful
* 2: Helpful
* 3: Cancel current targetting (server sent)
  
The model # should never be trusted.  
  
Once the server requests the client to target something, the server can undo this also. You can "kill" the targeting cursor faithfully and without any bugs. Send another Packet with Type set to 0 (Object), Cursor ID 00000000, and Cursor Type as 3\. The client will respond to this by sending a response to the original request just as if the user had hit ESC to cancel targeting.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Secure Trading  
Last Modified: 2009-08-15 09:14:23  
Modified By: MuadDib  
  
**Packet: 0x6F** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x6F  
BYTE\[2\] Length   
BYTE\[1\] Type Flag (see notes)  
BYTE\[4\] Player Serial  
BYTE\[4\] Container 1 Serial  
BYTE\[4\] Container 2 Serial  
BYTE\[1\] Has Name  
IF (Has Name = 1)   
BYTE\[?\] Player Name  

  
**Subcommand Build**  
N/A

  
**Notes**  
Type Flag  
0x00: Start  
0x01: Cancel  
0x02: Update  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Bulletin Board Messages  
Last Modified: 2009-08-15 10:58:09  
Modified By: MuadDib  
  
**Packet: 0x71** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x71  
BYTE\[2\] Length  
BYTE\[1\] Subcommand  
BYTE\[len-4\] Subcommand Details  

  
**Subcommand Build**  
Subcommand 0 - Display Bulletin Board (Server)  
BYTE\[4\] BoardID  
BYTE\[22\] board name (default is "bulletin board", the rest nulls)  
BYTE\[4\] ID (0x402000FF)  
BYTE\[4\] zero (0)  
  
Subcommand 1 - Message Summary (Server)  
BYTE\[4\] BoardID  
BYTE\[4\] MessageID  
BYTE\[4\] ParentID (0 if top level)  
BYTE\[1\] posterLen  
BYTE\[posterLen\] poster (null terminated string)  
BYTE\[1\] subjectLen  
BYTE\[subjectLen\] subject (null terminated string)  
BYTE\[1\] timeLen  
BYTE\[timeLen\] time (null terminated string with time of posting) ("Day 1 @ 11:28")  
  
Subcommand 2 - Message (Server)  
BYTE\[4\] BoardID  
BYTE\[4\] MessageID  
BYTE\[1\] posterLen  
BYTE\[posterLen\] poster (null terminated string)  
BYTE\[1\] subjectLen  
BYTE\[subjectLen\] subject (null terminated string)  
BYTE\[1\] timeLen  
BYTE\[timeLen\] time (null terminated string with time of posting) ("Day 1 @ 11:28")  
BYTE\[29\] constant: (01 91 84 0A 06 1E FD 01 0B 15 2E 01 0B 17 0B 01 BB 20 46 04 66 13 F8 00 00 0E 75 00 00)  
BYTE\[1\] numlines  
For each line:  
BYTE\[1\] linelen  
BYTE\[linelen\] body (null terminated)  
  
Subcommand 3 - Request Message (Client)  
BYTE\[4\] BoardID  
BYTE\[4\] MessageID  
  
Subcommand 4 - Request Message Summary (Client)  
BYTE\[4\] BoardID  
BYTE\[4\] MessageID  
  
Subcommand 5 - Post a message (Client)  
BYTE\[4\] BoardID  
BYTE\[4\] Replying to ID (0 if this is a top level / non-reply post)  
BYTE\[1\] subjectLen (length of the subject, includes null termination)  
BYTE\[subjectLen\] subject (null terminated)  
BYTE\[1\] numlines  
**For each line:**  
* BYTE linelen
* BYTE\[linelen\] body (null terminated)
  
Subcommand 6 - Remove Posted Message (Client)  
BYTE\[4\] BoardID  
BYTE\[4\] MessageID  

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Request War Mode  
Last Modified: 2009-08-02 05:25:11  
Modified By: MuadDib  
  
**Packet: 0x72** 
Sent By: Both  
Size: 5 Bytes  

  
**Packet Build**  
BYTE\[1\] 0x72  
BYTE\[1\] Flag  
BYTE\[3\] unknown1 (always 00 32 00 in testing)  

  
**Subcommand Build**  
N/A

  
**Notes**  
**Flag:**  
* 0x00 - Normal
* 0x01 - Fighting
  
Server replies with 0x77 packet. POL Responds with 0x72 right back.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Ping Message  
Last Modified: 2009-08-02 20:14:04  
Modified By: MuadDib  
  
**Packet: 0x73** 
Sent By: Both  
Size: 2 Bytes  

  
**Packet Build**  
BYTE\[1\] 0x73  
BYTE\[1\] Sequence Number  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Book Header ( Old )  
Last Modified: 2010-01-08 02:06:00  
Modified By: Tomi  
  
**Packet: 0x93** 
Sent By: Both  
Size: 99 Bytes  

  
**Packet Build**  
BYTE\[1\] 0x93  
BYTE\[4\] Book Serial  
BYTE\[1\] Write Flag (see notes)  
BYTE\[1\] 0x1 (unknown)  
BYTE\[2\] Page Count  
BYTE\[60\] Title  
BYTE\[30\] Author  

  
**Subcommand Build**  
N/A

  
**Notes**  
Write Flag  
* 0: Not Writable
* 1: Writable
  
Server version of packet is followed by packet 0x66 for Book Contents.  
  
Client sends a 0x93 message on book close. Update packet for the server to handle changes. Write Flag through Page Count are all 0's on client response  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Dye Window  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x95** 
Sent By: Both  
Size: 9 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] itemID of dye target  
BYTE\[2\] ignored on send, model on return  
BYTE\[2\] model on send, color on return from client (default on server send is 0x0FAB)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** All Names (3D Client Only)  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x98** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[2\] blocksize  
BYTE\[4\] ID  
If (server-reply)  
BYTE\[30\] name (0 terminated)  

  
**Subcommand Build**  
N/A

  
**Notes**  
Only 3D clients send this packet. Server and client packet.  
Unsure as of when, 2D client added this ability also.  
  
Client asks for name of object with ID x. Server has to reply with ID + name. Client automatically knows names of items. Hence it only asks only for NPC/Player names nearby, but shows bars of items plus NPCs.  
  
Client request has 7 bytes, server-reply 37\. Triggered by Crtl + Shift.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Give Boat/House Placement View  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x99** 
Sent By: Both  
Size: 26 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[1\] request (0x01 from server, 0x00 from client)   
BYTE\[4\] ID of deed   
BYTE\[12\] unknown (all 0)  
BYTE\[2\] multi model (item model - 0x4000)  
BYTE\[6\] unknown (all 0)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Console Entry Prompt  
Last Modified: 2009-08-02 20:28:33  
Modified By: MuadDib  
  
**Packet: 0x9A** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x9A  
BYTE\[2\] Length  
BYTE\[4\] Serial  
BYTE\[4\] Prompt ID  
BYTE\[4\] Type  
BYTE\[?\] Text String (optional), 0x00 Terminated  

  
**Subcommand Build**  
N/A

  
**Notes**  
**Type**  
0: Server Sent is Request. Client responds with 0 when ESC pressed  
1: Client Reply  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Chat Text  
Last Modified: 2008-10-11 09:57:22  
Modified By: Pierce  
  
**Packet: 0xB3** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] Length  
BYTE\[3\] Unicode Language  
BYTE\[2\] UnKNOWN(00 00)  
BYTE\[1\] Text/Command Type  

  
**Subcommand Build**  
Type 0x41 (Change Password):  
BYTE\[?\] Unicode Password  
BYTE\[2\] Null Terminator (00 00)  
  
Type 0x58 (Close):  
BYTE\[2\] Null Terminator (00 00)  
  
Type 0x61 (Message):  
BYTE\[?\] Unicode Message  
BYTE\[2\] Null Terminator (00 00)  
  
Text 0x62 (Join Conference):  
BYTE\[2\] Holder (0x0022)  
BYTE\[?\] Unicode conference name  
BYTE\[2\] Holder (0x0022)  
BYTE\[2\] Holder (0x0020)  
BYTE\[?\] Unicode password if pass req'd  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x63 (Create New Conference):  
BYTE\[?\] Unicode conference name  
BYTE\[2\] Holder (0x007b) if pass entered  
BYTE\[?\] Unicode conference pass if pass entered  
BYTE\[2\] Holder (0x007d) if pass entered  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x64 (Rename Conference):  
BYTE\[?\] Unicode conference name (all text after /rename )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x65 (Send Private Message):  
BYTE\[?\] Unicode name (all text after /msg )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x66 (Ignore):  
BYTE\[?\] Unicode name (all text after +ignore )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x67 (Stop Ignoring):  
BYTE\[?\] Unicode name (all text after -ignore )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x68 (Toggle Ignore):  
BYTE\[?\] Unicode name (all text after /ignore )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x69 (Grant Speaking Priveleges):  
BYTE\[?\] Unicode name (all text after +voice )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x6A (Remove Speaking Priveleges):  
BYTE\[?\] Unicode name (all text after -voice )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x6B (Toggle Speaking Priveleges):  
BYTE\[?\] Unicode name (all text after /voice )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x6C (Grant Moderator Status):  
BYTE\[?\] Unicode name (all text after +ops )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x6D (Remove Moderator Status):  
BYTE\[?\] Unicode name (all text after -ops )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x6E (Toggle Moderator Status):  
BYTE\[?\] Unicode name (all text after /ops )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x6F (not receiving messages):  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x70 (receiving messages):  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x71 (Toggle receiving messages):  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x72 (show my character name):  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x73 (don't show my character name):  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x74 (Toggle showing my character name):  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x75 (Who is this player?):  
BYTE\[?\] Unicode text (all text after /whois )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x76 (Kick out of the conference?):  
BYTE\[?\] Unicode text (all text after /kick )  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x77 (only moderators have speaking priveleges by default):  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x78 (everyone has speaking priveleges by default):  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x79 (Toggle default speaking priveleges):  
BYTE\[2\] Null Teriminator (0x0000)  
  
Text 0x7A (emote)  
BYTE\[?\] Unicode text (all text after /emote or /em ):  
BYTE\[2\] Null Teriminator (0x0000)  

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Request/Char Profile  
Last Modified: 2010-01-07 08:53:48  
Modified By: Tomi  
  
**Packet: 0xB8** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
BYTE\[1\] mode (CLIENT ONLY! Does not exist in server message.)  
BYTE\[4\] id  
**If request, ends here.** 
**If Update request**  
* BYTE\[2\] cmdType (0x0001 - Update)
* BYTE\[2\] msglen (# of unicode characters)
* BYTE\[msglen\]\[2\] new profile, in unicode, not null terminated.
**Else If from server**  
* BYTE\[?\] title (not unicode, null terminated.)
* BYTE\[?\]\[2\] static profile ( unicode string can't be edited, null terminated )
* BYTE\[?\]\[2\] profile (in unicode, can be edited, null terminated )

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Ultima Messenger  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0xBB** 
Sent By: Both  
Size: 9 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] id1  
BYTE\[4\] id2  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Client Version  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0xBD** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
If (client-version of packet)  
BYTE\[len-3\] string stating the client version (0 terminated) (like: "1.26.4")  

  
**Subcommand Build**  
N/A

  
**Notes**  
Client version : client sends its version string (e.g "3.0.8j")  
Server version : 0xbd 0x0 0x3 (client replies with client version of this packet)  
  
Clients sends a client version of this packet ONCE at login (without server request.)  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Assist Version  
Last Modified: 2009-03-01 08:36:37  
Modified By: Turley  
  
**Packet: 0xBE** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
**If (client-version of packet)**   
* BYTE\[4\] Assist version (numeric)
* BYTE\[?\] string stating the client version (0 terminated) (like: "1.26.4")
**If (server-version of packet)**  
* BYTE\[4\] Allowed Assist version (numeric)

  
**Subcommand Build**  
N/A

  
**Notes**  
Server requests assist version via server side version of this packet, client replies with client side version of packet. If it's a client without Assist, allowed Assists version from server is relayed in client version packet. Because assist never has assist version 0, that would be a way to disallow it.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** General Information Packet  
Last Modified: 2009-12-16 10:06:52  
Modified By: Tomi  
  
**Packet: 0xBF** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[2\] Length  
BYTE\[2\] Subcommand  
BYTE\[length-5\] Subcommand details  

  
**Subcommand Build**  
**Subcommand 1: Initialize Fast Walk Prevention**  
BYTE\[4\] key1  
BYTE\[4\] key2  
BYTE\[4\] key3  
BYTE\[4\] key4  
BYTE\[4\] key5  
BYTE\[4\] key6  
  
**Subcommand 2: Add key to Fast Walk Stack**  
BYTE\[4\] newkey  
  
**Subcommand 4: "Close Generic Gump"**  
BYTE\[4\] dialogID // which gump to destroy (second ID in 0xB0 packet)  
BYTE\[4\] buttonId // response buttonID for packet 0xB1  
  
**Subcommand 5: Screen size**   
BYTE\[2\] unknown1 , always 0 ?  
BYTE\[2\] X  
BYTE\[2\] Y  
BYTE\[2\] unknown2  
  
**\---- Start of Subcommand 0x6 ----**  
**Subcommand 6: Party System**  
Subsubcommands for 6:  
Subsubcommand 1: Add a party member (4 bytes)  
BYTE\[4\] id (if 0, a targeting cursor appears)Client Message  
  
**Subsubcommand 1: Add party member(s) (1+ numMembers\*4)**  
BYTE\[1\] number of Members (total number of members in the party)  
Then, for each member in numMembers:  
BYTE\[4\] id  
Server Message  
  
**Subsubcommand 2: Remove a party member (4 bytes)**  
BYTE\[4\] id (if 0, a targeting cursor appears)  
Client message  
  
**Subsubcommand 2: Remove a party member (? Bytes)**  
BYTE\[1\] number of Members (total number of members in the new party)  
BYTE\[4\] idofPlayerRemoved  
Then, for each member in numMembers:  
BYTE\[4\] id  
Server message  
  
**Subsubcommand 3: Tell party member a message (Variable # of bytes)**  
BYTE\[4\] id (of target, from client, of source, from server)  
BYTE\[n\]\[2\] Null terminated Unicode message.  
Client & Server Message  
  
**Subsubcommand 4: Tell full party a message (Variable # of bytes)**  
BYTE\[n\]\[2\] Null terminated Unicode message.  
Client Message.  
  
**Subsubcommand 4: Tell full party a message (Variable # of bytes)**  
BYTE\[4\] id (of source)  
BYTE\[n\]\[2\] Null terminated Unicode message.  
Server Message  
  
**Subsubcommand 6: Party Can Loot Me? (1 byte)**  
BYTE\[1\] canloot (0=no, 1=yes)  
Client message  
  
**Subsubcommand 7: party invitation?**  
BYTE\[4\] serial (party leader)  
  
**Subsubcommand 8: Accept join party invitation (4 bytes)**  
BYTE\[4\] serial (party leader)  
Client message  
  
**Subsubcommand 9: Decline join party invitation (4 bytes)**  
BYTE\[4\] serial (party leader)  
Client message  
**\---- End of Subcommand 0x6 ----**  
  
**Subcommand 8: Set cursor hue / Set MAP**  
BYTE\[1\] hue (0 = Felucca, unhued / BRITANNIA map. 1 = Trammel, hued gold / BRITANNIA map, 2 = (switch to) ILSHENAR map)  
  
**Subcommand 0x0a: wrestling stun**  
Sent by using the client Wrestle Stun Macro key in Options. This is no longer used since AoS was introduced. The Macro selection that used it was removed.  
  
**Subcommand 0x0b: Client Language**  
BYTE\[3\] language (ENU, for English)   
  
**Subcommand 0x0c: Closed Status Gump**  
BYTE\[4\] id (character id)  
  
**Subcommand 0x0e: 3D Client Action**  
BYTE\[4\] Animation ID. See Notes for list.  
  
**Subcommand 0x0f: ClientType , send once at login**  
BYTE\[1\] unk1 0x0a  
BYTE\[4\] clienttype flag (like char create/login)  
  
**Subcommand 0x10: unknown, related to 0xD6 Mega Cliloc somehow**  
BYTE\[4\] Item ID  
BYTE\[4\] Unknown  
  
**Subcommand 0x13: Request popup menu**  
BYTE\[4\] id (character id)  
  
**Subcommand 0x14: Display Popup/context menu (2D and KR)**  
BYTE\[1\] unknown (0x00)  
BYTE\[1\] subsubcommand (0x01 for 2D, 0x02 for KR)  
BYTE\[4\] Serial   
BYTE\[1\] Number of entries in the popup/context  
  
2D-client:  
loop Entries   
BYTE\[2\] Unique ID (returned by client)  
BYTE\[2\] Cliloc ID (ID is Cliloc - 3000000)  
BYTE\[2\] Flags 0x00=enabled, 0x01=disabled, 0x02=arrow, 0x20 = color  
If (Flags = 0x20)  
BYTE\[2\] color/hue; // rgb 1555 color (ex, 0 = transparent, 0x8000 = solid black, 0x1F = blue, 0x3E0 = green, 0x7C00 = red)  
Endif  
endloop(entries)  
  
KR-client:  
loop Entries   
BYTE\[4\] Text ID Number?  
BYTE\[2\] Index of entry (entry tag)?  
BYTE\[2\] Flags 0x00=enabled, 0x01=disabled, 0x04 = highlighted  
endloop(entries)  
  
**Subcommand 0x15: Popup Entry Selection**  
BYTE\[4\] Character ID   
BYTE\[2\] Entry Tag for line selected provided in subcommand 0x14   
  
**Subcommand 0x16: Close User Interface Windows**  
BYTE\[4\] Window ID  
  
Window ID:s  
 0x01: Paperdoll  
 0x02: Status  
 0x08: Character Profile  
 0x0C: Container  
  
BYTE\[4\] if ( Window ID == 0x0C ) Container Serial, Else character serial  
  
**Subcommand 0x17: Codex of wisdom**  
BYTE\[1\] unknown, always 1\. if not 1, packet seems to have no effect   
BYTE\[4\] msg number   
BYTE\[1\] presentation (0: flashing, 1: directly opening)  
  
**Subcommand 0x18: Enable map-diff (files)**  
BYTE\[4\] Number of maps   
**For each map**  
* BYTE\[4\] Number of map patches in this map
* BYTE\[4\] Number of static patches in this map
  
**Subcommand: 0x19: Extended stats**  
BYTE\[1\] subsubcommand (0x2 for 2D client, 0x5 for KR client)   
BYTE\[4\] serial   
BYTE\[1\] unknown (always 0)   
BYTE\[1\] Lock flags (0 = up, 1 = down, 2 = locked, FF = update mobile status animation ( KR only )  
  
Lock flags = 00SSDDII ( in binary )  
00 = up  
01 = down  
10 = locked  
  
If(subsubcommand = 0x05 (KR))  
  
 If(Lock flags = 0xFF) //Update mobile status animation  
 BYTE\[1\] Status // Unveryfied if lock flags == FF the locks will be handled here  
 BYTE\[1\] unknown (0x00)   
 BYTE\[1\] Animation   
 BYTE\[1\] unknown (0x00)   
 BYTE\[1\] Frame   
 else  
 BYTE\[1\] unknown (0x00)   
 BYTE\[4\] unknown (0x00000000)  
 endif  
  
endif  
  
**Subcommand: 0x1a: Extended stats**  
BYTE\[1\] stat // 0: str, 1: dex, 2:int   
BYTE\[1\] status // 0: up, 1:down, 2: locked   
  
**Subcommand 0x1b: New Spellbook**  
BYTE\[2\] unknown, always 1   
BYTE\[4\] Spellbook serial   
BYTE\[2\] Item Id  
BYTE\[2\] scroll offset // 1==regular, 101=necro, 201=paladin, 401=bushido, 501=ninjitsu, 601=spellweaving  
BYTE\[8\] spellbook content // first bit of first byte = spell #1, second bit of first byte = spell #2, first bit of second byte = spell #8, etc   
  
**Subcommand 0x1c: Spell selected, client side**  
BYTE\[2\] unknown, always 2   
BYTE\[2\] selected spell(0-indexed)+scroll offset from sub 0x1b  
  
**Subcommand 0x1D: Send House Revision State**  
byte\[4\] houseserial  
byte\[4\] revision state  
  
**Subcommand 0x1E:**   
byte\[4\] houseserial  
  
**Subcommand 0x21: (AOS) Ability icon confirm.**  
Note: no data, just (bf 00 05 21)  
  
**Subcommand 0x22: Damage**  
BYTE\[2\] unknown, always 1   
BYTE\[4\] Serial   
BYTE\[1\] Damage // how much damage was done ?  
  
**Subcommand 0x24: UnKnown**  
BYTE\[1\] unknown. UOSE Introduced  
  
**Subcommand 0x25: SE Ability Change**  
BYTE\[1\] Ability ID  
BYTE\[1\] 0/1 On/Off  
  
**Subcommand 0x26: Mount Speed**  
BYTE\[1\] 0=normal,1=fast,2=slow, >2=Hybrid movement?  
  
**Subcommand 0x2A: Change Race Request (only 2D, Server packet)**  
BYTE\[1\] Female (1=true, 0=false)  
BYTE\[1\] Race (1=human, 2=elf, 3=gargoyle, 255=error)  
  
**Subcommand 0x2A: Change Race Response (only 2D, Client packet)**  
BYTE\[2\] Skin color  
BYTE\[2\] Hair style  
BYTE\[2\] Hair color  
BYTE\[2\] Beard style  
BYTE\[2\] Beard color  
  
**Subcommand 0x2C: Use targeted item (client side packet)**  
Byte\[4\] Item Serial  
Byte\[4\] Target Serial  
  
**Subcommand 0x2D: Cast targeted spell (client side packet)**  
Byte\[2\] Spell ID  
Byte\[4\] Target Serial  
  
**Subcommand 0x2E: Use targeted skill (client side packet)**  
Byte\[2\] Skill ID (1 to 55, if Skill Id = 0 -> last skill)  
Byte\[4\] Target Serial  
  
**Subcommand 0x2F: KR House Menu Gump**  
todo: there are a lot of subsub commands...  
  
**Subcommand 0x32: Toggle gargoyle flying**  
Byte\[4\] unk1 (always 0x0100)  
Byte\[2\] unk2 (always 0x0)  

  
**Notes**  
**Subcommand 1:** Server Sent. This sets up stack on the client and whenever it moves, it takes the top value from this stack and uses it. (key1 starts at the top, key6 at the bottom)  
  
**Subcommand 2:** Server Sent. This key is added to the top of the stack. In other words, it's the one that will be used next. Basically, the other 5 only get used when the client is sending moves faster than the server is responding.  
  
**Subcommand 4:** Server Sent.  
  
**Subcommand 5:** Client Sent.  
  
**Subcommand 6:** Sent by both. Contains subcommands for 0x6 for the party system. Be sure you check the packet listing good before attempting these.  
  
**Subcommand 8:** Server Sent.  
  
**Subcommand a:** Read above. Client Sent.  
  
**Subcommand b:** Client Sent once at login of character.  
  
**Subcommand c:** Client Sent when they close their status bar.  
  
**Subcommand e:** Client Sent. Server responds with Play Animation packets.  
List of animation IDs:  
00 00 00 06 - Yawn  
00 00 00 15 - Faint  
00 00 00 20 - Bow  
00 00 00 21 - Salute  
00 00 00 64 - Applaud  
00 00 00 66 - Argue  
00 00 00 68 - Blow Kiss  
00 00 00 69 - Formal Bow  
00 00 00 6B - Cover Ears  
00 00 00 6C - Curtsey  
00 00 00 6D - Jig  
00 00 00 6E - Folk Dance  
00 00 00 6F - Dance  
00 00 00 70 - Tribal Dance  
00 00 00 71 - Fold Arms  
00 00 00 72 - Impatient  
00 00 00 73 - Lecture  
00 00 00 74 - Nod  
00 00 00 75 - Point  
00 00 00 77 - Greet Salute  
00 00 00 79 - Shake Head  
00 00 00 7B - Victory  
00 00 00 7C - Celebrate  
00 00 00 7D - Wave  
00 00 00 7E - Two Handed Wave  
00 00 00 7F - Long Distance Wave  
00 00 00 80 - What?  
  
**Subcommand f:** Client Sent.  
  
**Subcommand 13:** Client Sent.  
  
**Subcommand 14:** Server Sent. TextID is broken into two decimal parts:  
stringID / 1000: intloc fileID  
stringID % 1000: text index  
  
So, say you want the 123rd text entry of intloc06, the stringID would be 6123\. Newer clients without the intloc files, that use single Cliloc files, use the math ID - 3000000 for the ID to send.  
  
**Subcommand 15:** Client Sent.  
  
**Subcommand 16:** Server Sent.  
  
**Subcommand 17:** Server Sent. Shows codex of wisdom's text #msg. (msg is linearised (including sub indices) index number starting with 1).  
  
**Subcommand 18:** Server Sent. Number of maps: currently 3 (0 = Fellucca, 1 = Trammel, and 2 = Ilshenar). This packet is sent by the server to the client, telling the client to use the mapdif\* and stadif\* files for patching map and statics.  
  
**Subcommand 19:** Server Sent. Is sent after the 0x11 packet to enable the stat locks and show their states on 4.x+ clients.  
  
**Subcommand 1a:** Client Sent. Stat Lock change packet. Sent when client changes the lock state for a stat on the status bar.  
  
**Subcommand 1b:** Server Sent. Related to new spell packet 0xBF Subcommand 0x1C.  
  
**Subcommand 1c:** Client Sent. Related to new spellbook packet 0xBF Subcommand 0x1b.  
  
**Subcommand 1d:** Server Sent. Part of AoS Custom Housing. Sends a house Revision number for handling client multi cache. If revision is newer than what client has it asks for the new multi packets to cache it.  
  
**Subcommand 1e:** Client Sent. Part of AoS Custom Housing. Sends a houses ID number back after recieving the 0x1D subcommand  
  
**Subcommand 21:** Server Sent. This is sent to reset the color of the icons, and also sent when the server denies the ability attempt (not enough skill or mana).  
  
**Subcommand 22:** Server Sent. Packet for displaying damage dealt above the mobs head.  
  
**Subcommand 24:** Client Sent. Unknown.  
  
**Subcommand 0x25:** Server Sent. Toggles SE Abilities.  
  
**Subcommand 0x26:** Server Sent. Toggles Running at mount speed, even when not mounted.  
  
**Subcommand 0x2C:** Client Sent. For use with the new Bandage Self client macro. Introduced in 5.0.4x  
  
**Subcommand 0x2D:** Spell Ids  
0x01 - 0x40 - Mage Spells  
0x65 - 0x75 - Necromancer Spells  
0xC9 - 0xD2 - Paladin Spells  
0x91 - 0x96 - Samurai Spells  
0xF5 - 0xFC - Ninja Spells  
0x59 - 0x68 - Spellweaving Spells  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Unicode TextEntry  
Last Modified: 2009-03-01 07:58:48  
Modified By: Turley  
  
**Packet: 0xC2** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[2\] length  
BYTE\[4\] player id   
BYTE\[4\] message id  
If (server\_version of this packet)  
* BYTE\[10\] all 0's .. have to be there, otherwise client crash
If (client\_version)  
* BYTE\[4\] unknown, always 1
* BYTE\[3\] language code, i.e. DEA, ENU etc.
* BYTE\[?\] unicode Text

  
**Subcommand Build**  
N/A

  
**Notes**  
Client version is the reply packet. Server starts with a server side 0xc2\. server can pick any message id to identify it. The next text entry from client after 0xc2 server side is client reply with a 0xc2 sent back. First 11 bytes relayed from server packet, plus 0 0 0 1 plus 3 bytes language code + Unicode text. Server side packet always has length 0x15.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Client View Range  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0xC8** 
Sent By: Both  
Size: 2 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] range  

  
**Subcommand Build**  
N/A

  
**Notes**  
Range: how far away client wants to see (=get send) items/npcs  
Since client 3.0.8o it actually got activated.(in a useful way) When increase/decrease macro send, client sends a 0xc8\. If and only if erver "relays" the packet (sending back the same data) range stuff gets activated client side.  
  
"Greying" has no packet, purely client internal.  
  
Minimal value:5, maximal: 18  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Get Area Server Ping  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0xC9** 
Sent By: Both  
Size: 6 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] sequence // initially 1, wraps after 255 to 0  
BYTE\[4\] tickcount  

  
**Subcommand Build**  
N/A

  
**Notes**  
Client sends packet with it's tick count. Server replies with same packetID/sequence and it's own tick count. Over a few packets, the client can deduct a reasonably accurate ping number. Currently only used/ implemented in god client  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Get User Server Ping  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0xCA** 
Sent By: Both  
Size: 6 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] sequence // initially 1, wraps after 255 to 0  
BYTE\[4\] tickcount  

  
**Subcommand Build**  
N/A

  
**Notes**  
See 0xC9  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Configuration File  
Last Modified: 2009-03-01 08:40:15  
Modified By: Turley  
  
**Packet: 0xD0** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[2\] length  
BYTE\[1\] type - file type, not sure of precise values   
BYTE\[length-4\] unknown encoded in some form  

  
**Subcommand Build**  
N/A

  
**Notes**  
Sent if (flag & 0x02) in packet 0xA9  
Probably used for saving client configuration files server side. OSI used it for the IGR shard only that is not anymore. Not sure what configuration files are, perhaps those in /desktop/player dir  
  
Encoding/(compression?) completely unknown  
Note: there's a client and server version of it.  
If on, client sends them 5 times (5 files) at login.  
After logout (0xd1) it expects server versions of 0xd0 and finally a server version of 0xd1  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Logout Status  
Last Modified: 2009-08-15 06:50:03  
Modified By: MuadDib  
  
**Packet: 0xD1** 
Sent By: Both  
Size: 2 bytes  

  
**Packet Build**  
BYTE\[1\] 0xD1  
BYTE\[1\] 0x01  

  
**Subcommand Build**  
N/A

  
**Notes**  
Client will send this packet without 0x01 Byte when the server sends FLAG & 0x02 in the 0xA9 Packet during logon.  
  
Server responds with same packet, plus the 0x01 Byte, allowing client to finish logging out.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Book Header ( New )  
Last Modified: 2010-01-08 02:05:39  
Modified By: Tomi  
  
**Packet: 0xD4** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[2\] length   
BYTE\[4\] book ID   
BYTE\[1\] flag1   
BYTE\[1\] flag2   
BYTE\[2\] number of pages   
BYTE\[2\] length of author string (0 terminator included)   
BYTE\[?\] author (ASCII, 0 terminated)   
BYTE\[2\] length of title string (0 terminator included)   
BYTE\[?\] title string  

  
**Subcommand Build**  
N/A

  
**Notes**  
Packet sent from server when opening a book: opening book writeable: flag1+flag2 both 1 (opening readonly book: flag1+flag2 0, unverified though)  
  
client side: flag1+flag2 both 0, number of pages 0.  
  
Example::  
Opening books goes like this:  
open book ->   
server sends 0xd4(title + author)  
server sends 0x66 with all book data "beyond" title + author  
  
if title + author changed: client side 0xd4 send  
  
if other book pages changed: 0x66 client side send.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Mega Cliloc  
Last Modified: 2009-08-01 18:25:44  
Modified By: MuadDib  
  
**Packet: 0xD6** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
**Server Version Of Packet**  
BYTE\[1\] 0xD6  
BYTE\[2\] Length  
BYTE\[2\] 0x0001  
BYTE\[4\] Serial of item/creature  
BYTE\[2\] 0x0000  
BYTE\[4\] Serial of item/creature in all tests. This could be the serial of the item the entry to appear over.  
  
Loop of all the item/creature's properties to display in the order to display them. The name is always the first entry.  
BYTE\[4\] Cliloc ID  
BYTE\[2\] Length of (if any) Text to add into/with the cliloc  
BYTE\[?\] Unicode text to be added into the cliloc. Not sent if Length of text above is 0  
  
BYTE\[4\] 00000000 - Sent as end of packet/loop  
  
**Client Version Of Packet**  
BYTE\[1\] 0xD6  
BYTE\[2\] Length  
Loop for each serial being sent to request tooltip detail about  
BYTE\[4\] Serial  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Generic AOS Commands  
Last Modified: 2009-03-06 04:46:05  
Modified By: MuadDib  
  
**Packet: 0xD7** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[2\] Length  
BYTE\[4\] Player Serial  
BYTE\[2\] SubCommand to Follow  

  
**Subcommand Build**  
**SubCommand 0x02: Backup**  
BYTE\[1\] 07  
  
SubCommand 0x03: Restore  
BYTE\[1\] 07  
  
SubCommand 0x04: Commit  
BYTE\[1\] 07  
  
SubCommand 0x05: DeleteItem  
BYTE\[1\] unknown (0)  
BYTE\[2\] unknown (0)  
BYTE\[2\] item graphic  
BYTE\[1\] unknown (0)  
BYTE\[4\] X Pos  
BYTE\[1\] unknown (0)  
BYTE\[4\] Y Pos  
BYTE\[1\] unknown (0)  
BYTE\[4\] z Pos (7)  
BYTE\[1\] terminator (0x07)  
  
SubCommand 0x06: AddItem  
BYTE\[1\] unknown (0)  
BYTE\[2\] unknown (0)  
BYTE\[2\] itemGraphic  
BYTE\[1\] unknown (0)  
BYTE\[4\] XPos  
BYTE\[1\] unknown (0)  
BYTE\[4\] YPos  
BYTE\[1\] terminator (0x07)  
  
SubCommand 0x0C: Exit House Tool  
BYTE\[1\] unknwon (0x07)  
  
SubCommand 0x0D: Change Stairs  
BYTE\[1\] unknown (0x00)  
BYTE\[2\] X Position (Relative to center of house)  
BYTE\[1\] unknown (0x00)  
BYTE\[2\] Y Position (Relative to center of house)  
BYTE\[1\] unknown (0x00)  
  
SubCommand 0x0E: Synch Button  
BYTE\[1\] Unknown (0x07)  
  
SubCommand 0x10: Clear Button  
BYTE\[1\] Unknown (0x07)  
  
SubCommand 0x12: ChangeFloor  
BYTE\[4\] unknown (0)  
BYTE\[1\] Floor#  
BYTE\[1\] terminator (0x07)  
  
SubCommand 0x1A: Revert Button  
BYTE\[1\] Unknown (0x07)  
  
SubCommand 0x19: Combat Book Abilities  
BYTE\[4\] 00 00 00 00 = Unknown. Always like this in all my testing   
BYTE\[1\] The ability "number" used  
BYTE\[1\] 0A  
  
SubCommand 0x28: Guild Button  
BYTE\[1\] Unknown (0x07)  
  
SubCommand 0x32: Quest Button  
BYTE\[1\] Unknown (0x07)  

  
**Notes**  
Submitted information. untested and unverified.  
  
Subcommand 0x02: This is sent when the client pushed the Backup button in the customization screen. Unsure what it triggers on the server since house building is totally handled client side until submitted.  
  
SubCommand 0x03: Sent when client pushes the Restore button in the customization menu. Will need testing to see exact purpose of 2 and 3 subs. Possible the server could request current "layout" that is not yet commited to save the design without commiting to be later restored with this command.  
  
SubCommand 0x04: Sent when client pushes the Commit Button.  
  
SubCommand 0x05: Sent when client chooses to destroy items.  
  
SubCommand 0x06: Sent when client Chooses to add items.  
  
SubCommand 0x0C: Sent when client pushes the Exit Button.  
  
SubCommand 0x0D: Sent when client adds stairs to the multi in edit mode.  
  
SubCommand 0x0E: Sent when client pushes the Synch Button.  
  
SubCommand 0x10: Sent when client pushes the Clear Button.  
  
SubCommand 0x12: Sent when client pushes the Change Floor Buttons.  
  
SubCommand 0x1A: Sent when client pushes the Revert Button.  
  
SubCommand 0x19: Sent when client pushes the icones from the combat book.  
The server uses an 0xBF Subcommand 0x21 Packet to cancel the red color of  
icons, and reset the status of them on client.  
Valid Ability Numbers:  
0x00 = Cancel Ability Attempt   
0x01 = Armor Ignore   
0x02 = Bleed Attack   
0x03 = Concusion Blow   
0x04 = Crushing Blow   
0x05 = Disarm   
0x06 = Dismount   
0x07 = Double Strike   
0x08 = Infecting   
0x09 = Mortal Strike   
0x0A = Moving Shot   
0x0B = Paralyzing Blow   
0x0C = Shadow Strike   
0x0D = Whirlwind Attack   
0x0E = Riding Swipe   
0x0F = Frenzied Whirlwind   
0x10 = Block   
0x11 = Defense Mastery   
0x12 = Nerve Strike   
0x13 = Talon Strike   
0x14 = Feint   
0x15 = Dual Wield   
0x16 = Double shot   
0x17 = Armor Peirce   
0x18 = Bladeweave   
0x19 = Force Arrow   
0x1A = Lightning Arrow   
0x1B = Psychic Attack   
0x1C = Serpent Arrow   
0x1D = Force of Nature  
  
SubCommand 0x28: Sent when client pushes the Guild Button on the paperdoll.  
  
SubCommand 0x32: Sent when client pushes the Quest Button on the paperdoll.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Freeshard List  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0xF1** 
Sent By: Both  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] Length  
BYTE\[1\] Sub command  

  
**Subcommand Build**  
Subcommand 0xFF:  
 From Client (UOG or CUO):  
  
 From Server:  

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Edit Tile Data (God Client)  
Last Modified: 2009-08-06 04:24:01  
Modified By: Turley  
  
**Packet: 0x0C** 
Sent By: Both?  
Size: variable  

  
**Packet Build**  
BYTE\[1\] Cmd  
BYTE\[2\] Length  
BYTE\[2\] Tile Number to edit. If number is 0x8000+, then it is a map tile.  
BYTE\[4\] Tile data Flags for the item.  
BYTE\[1\] Weight of item.  
BYTE\[1\] Quality of item.  
BYTE\[2\] Unknown.  
BYTE\[1\] Unknown.  
BYTE\[1\] Quantity.  
BYTE\[2\] Frame number of the animation.  
BYTE\[1\] Unknown.  
BYTE\[1\] Hue.  
BYTE\[1\] Unknown.  
BYTE\[1\] Value of item.  
BYTE\[1\] Height.  
BYTE\[20\] Item's Name.  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Create Character  
Last Modified: 2009-12-03 01:06:03  
Modified By: Turley  
  
**Packet: 0x00** 
Sent By: Client  
Size: 104 bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[4\] pattern1 (0xedededed)   
BYTE\[4\] pattern2 (0xffffffff)   
BYTE\[1\] pattern3 (0x00)   
BYTE\[30\] char name   
BYTE\[2\] unknown0  
BYTE\[4\] clientflag (see notes)  
BYTE\[4\] unknown1  
BYTE\[4\] logincount  
BYTE\[1\] profession  
BYTE\[15\] unknown2  
BYTE\[1\] sex (see notes)  
BYTE\[1\] str   
BYTE\[1\] dex   
BYTE\[1\] int   
BYTE\[1\] skill1   
BYTE\[1\] skill1value   
BYTE\[1\] skill2   
BYTE\[1\] skill2value   
BYTE\[1\] skill3   
BYTE\[1\] skill3value   
BYTE\[2\] skinColor   
BYTE\[2\] hairStyle   
BYTE\[2\] hairColor   
BYTE\[2\] facial hair   
BYTE\[2\] facial hair color   
BYTE\[2\] location # from starting list   
BYTE\[2\] unknown3 (Usually 0x00 in testing)  
BYTE\[2\] slot  
BYTE\[4\] clientIP   
BYTE\[2\] shirt color  
BYTE\[2\] pants color  

  
**Subcommand Build**  
N/A

  
**Notes**  
Note: pattern3 is set to 0xFF to early signal a Krrios client user, where it expects a 0xF0 | 0x00 before login confirm  
  
clientflag:  
t2a: 0x00,  
renaissance: 0x01,  
third dawn: 0x02,  
lbr: 0x04,  
aos: 0x08,  
se: 0x10,  
sa: 0x20,  
uo3d: 0x40,  
reserved: 0x80,  
3dclient: 0x100  
Sex: 0=Male Human, 1=Female Human, 2=Male Elf, 3=Female Elf  
Since client 7.0:  
Sex: 0=Male Human, 1=Female Human, 2=Male Human, 3=Female Human, 4=Male Elf, 5=Female Elf, 6=Male Gargoyle, 7=Female Gargoyle  
Str, dex and int should always sum to 65.  
Str, dex and int should always be between 10 and 45, inclusive.  
Skill1, skill2, and skill3 should never be the same value.   
Skill1, skill2, and skill3 should always be between 0 and 45, inclusive.  
Skill1value, skill2value, and skill3value should always sum to 100.  
Skill1value, skill2value, and skill3value should always be between 0 and 50, inclusive.  
SkinColor should always be between 0x3EA and 0x422, exclusive.  
HairColor and facialHairColor should always be between 0x44E and 0x4AD, exclusive.  
HairStyle should be between 0x203B and 0x204A, exclusive, and it should also exclude 0x203D to 0x2044, exclusive.  
FacialHairStyle should be between 0x203E and 0x204D  
Shirt color and Pants color need bounds checking too.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Disconnect Notification  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x01** 
Sent By: Client  
Size: 5 bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] 0xFFFFFFFF  

  
**Subcommand Build**  
N/A

  
**Notes**  
Sent only when client selects "Log Off" ingame, and returns to the main menu.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Move Request  
Last Modified: 2009-03-02 03:46:22  
Modified By: Turley  
  
**Packet: 0x02** 
Sent By: Client  
Size: 7 bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] direction  
BYTE\[1\] sequence number   
BYTE\[4\] fastwalk prevention key  

  
**Subcommand Build**  
N/A

  
**Notes**  
Sequence number starts at 0, after a reset. However, if 255 is reached, the next seq # is 1, not 0.  
  
Fastwalk prevention notes: each 0x02 pops the top element from fastwalk key stack. (0xbf sub1 init. fastwalk stack, 0xbf sub2 pushes an element to stack)  
If stack is empty key value is 0\. (-> never set keys to 0 in 0xbf sub 1/2)  
Because client sometimes sends bursts of 0x02's DON'T check for a certain top stack value.  
The only safe way to detect fastwalk: push a key after EACH 0x21, 0x22, (=send 0xbf sub 2) check in 0x02 for stack emptyness.  
  
If empty -> fastwalk alert.  
Note that actual key values are irrelevant. (just don't use 0)  
Of course without perfect 0x02/0x21/0x22 synch (serverside) it's useless to use fastwalk detection.  
Last but not least: fastwalk detection adds 9 bytes per step and player !  
  
Direction Notes:  
0x00 - North  
0x01 - Northeast  
0x02 - East  
0x03 - Southeast  
0x04 - South  
0x05 - Southwest  
0x06 - West  
0x07 - Northwest  
  
Running affects the Direction number also. When running, it is Direction|0x80\. Therefore direction becomes 0x80, 0x81, and so on.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Talk Request  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x03** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
BYTE\[1\] SpeechType   
BYTE\[2\] Color  
BYTE\[2\] SpeechFont   
BYTE\[?\] msg - Null Terminated (blockSize - 8)  

  
**Subcommand Build**  
N/A

  
**Notes**  
The various types of text is as follows:  
0x00 - Normal   
0x01 - Broadcast/System  
0x02 - Emote   
0x06 - System/Lower Corner  
0x07 - Message/Corner With Name  
0x08 - Whisper  
0x09 - Yell  
0x0A - Spell  
0x0D - Guild Chat  
0x0E - Alliance Chat  
0x0F - Command Prompts  
0xC0 - Encoded Commands  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Request God Mode (God Client)  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x04** 
Sent By: Client  
Size: 2 bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] mode (0=off, 1=on)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Request Attack  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x05** 
Sent By: Client  
Size: 5 bytes  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[4\] ID to be attacked  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Double Click  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x06** 
Sent By: Client  
Size: 5 bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] ID of double clicked object  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Pick Up Item  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x07** 
Sent By: Client  
Size: 7 bytes  

  
**Packet Build**  
BYTE\[1\] CMD  
BYTE\[4\] Item Serial  
BYTE\[2\] # of items in stack  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Drop Item  
Last Modified: 2009-08-13 09:52:18  
Modified By: MuadDib  
  
**Packet: 0x08** 
Sent By: Client  
Size: 14/15 bytes  

  
**Packet Build**  
2D Client Version (before v6.0.1.7)  
BYTE\[1\] 0x08  
BYTE\[4\] Item Serial  
BYTE\[2\] X Location   
BYTE\[2\] Y Location  
BYTE\[1\] Z Location  
BYTE\[4\] Container Serial Dropped Onto (FF FF FF FF drop to ground)   
  
3D Client (UOKR+) and Legacy 2D 6.0.1.7+ Version  
BYTE\[1\] 0x08  
BYTE\[4\] Item Serial  
BYTE\[2\] X Location   
BYTE\[2\] Y Location  
BYTE\[1\] Z Location  
BYTE\[1\] Backpack grid index (see notes)  
BYTE\[4\] Container Serial Dropped Onto (FF FF FF FF drop to ground)  

  
**Subcommand Build**  
N/A

  
**Notes**  
The backpack grid index exists since 6.0.1.7 2D and 2.45.5.6 KR.  
Older clients are sending 14 bytes without this grid index byte!  
  
Older Notes:  
3D clients sometimes sends 2 of them (bursts) for ONE drop action. The last one having -1's in X/Y locs.  
  
Be very careful how to handle this odd "bursts" server side, neither always process, nor always skipping is correct.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Single Click  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x09** 
Sent By: Client  
Size: 5 bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] ID of single clicked object  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Edit (God Client)  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x0A** 
Sent By: Client  
Size: 11  

  
**Packet Build**  
BYTE\[1\] Cmd  
BYTE\[1\] Subcommand  
BYTE\[2\] The new object's x-axis position. If Command is HackMove Request, this will be a boolean value (still 16-bit) representing the requested mode.  
BYTE\[2\] The new object's y-axis position.  
BYTE\[2\] The new object's id. If Command is Add New NPC, this is the template number.  
BYTE\[1\] The new object's z-axis position.  
BYTE\[2\] Hue/Color.  

  
**Subcommand Build**  
Subcommand 4: Add New Dynamic Item.  
Subcommand 6: Hackmove Request.  
Subcommand 7: Add New NPC.  
Subcommand A: Add New Static Item.  

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Request Skill etc use  
Last Modified: 2009-03-02 04:21:47  
Modified By: Turley  
  
**Packet: 0x12** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE cmd  
BYTE\[2\] blockSize   
BYTE\[1\] type   
Types Listed:  
 <li**0x24 ($) - skill** 
   * BYTE\[blockSize-4\] skill (null terminated strings)
      * "1 0" - anatomy
      * "2 0" - animal lore
      * "3 0" - item identification
      * "4 0" - arms lore
      * "6 0" - begging
      * "9 0" - peacemaking
      * "12 0" - cartography
      * "14 0" - detect hidden
      * "15 0" - entice
      * "16 0" - evaluate intelligence
      * "19 0" - forensic evaluation
      * "21 0" - hiding
      * "22 0" - provocation
      * "23 0" - inscription
      * "30 0" - poisoning
      * "32 0" - spirit speak
      * "33 0" - stealing
      * "35 0" - taming
      * "36 0" - taste identification
      * "38 0" - tracking
* **0x56 (V) - Macro'd Spell**
   * BYTE\[blockSize-4\] Spell (null terminated strings)
      * "2" - Create Food
      * "3" - Feeblemind
      * "4" - Heal
      * "5" - Magic Arrow
      * "6" - Night Sight
      * "7" - Reactive Armor
      * "8" - Weaken
      * "9" - Agility
      * "10" - Cunning
      * "11" - Cure
      * "12" - Harm
      * "13" - Magic Trap
      * "14" - Magic Untrap
      * "15" - Protection
      * "16" - Strength
      * "17" - Bless
      * "18" - Fireball
      * "19" - Magic Lock
      * "20" - Poison
      * "21" - Telekenisis
      * "22" - Teleport
      * "23" - Unlock
      * "24" - Wall of Stone
      * "25" - Arch Cure
      * "26" - Arch Protection
      * "27" - Curse
      * "28" - Fire Field
      * "29" - Greater Heal
      * "30" - Lightning
      * "31" - Mana Drain
      * "32" - Recall
      * "33" - Blade Spirit
      * "34" - Dispel Field
      * "35" - Incognito
      * "36" - Reflection
      * "37" - Mind Blast
      * "38" - Paralyze
      * "39" - Poison Field
      * "40" - Summon Creature
      * "41" - Dispel
      * "42" - Energy Bolt
      * "43" - Explosion
      * "44" - Invisibility
      * "45" - Mark
      * "46" - Mass Curse
      * "47" - Paralyze Field
      * "48" - Reveal
      * "49" - Chain Lightning
      * "50" - Energy Field
      * "51" - Flame Strike
      * "52" - Gate
      * "53" - Mana Vampire
      * "54" - Mass Dispel
      * "55" - Meteor Shower
      * "56" - Polymorph
      * "57" - Earthquake
      * "58" - Energy Vortex
      * "59" - Ressurection
      * "60" - Summon Air Elemental
      * "61" - Summon Daemon
      * "62" - Summon Earth Elemental
      * "63" - Summon Fire Elemental
      * "64" - Summon Water Elemental
* **0x58 (X) - Open Door**
   * BYTE\[1\] null termination (0x00)
* **0xc7 - action**
   * BYTE\[blockSize-4\] Action (null terminated strings)
      * "bow"
      * "salute"

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Drop->Wear Item  
Last Modified: 2009-03-02 05:08:35  
Modified By: Turley  
  
**Packet: 0x13** 
Sent By: Client  
Size: 10 bytes  

  
**Packet Build**  
BYTE cmd  
BYTE\[4\] itemid   
BYTE layer (layer of player)   
BYTE\[4\] playerID  

  
**Subcommand Build**  
N/A

  
**Notes**  
1.) The Layer should not be trusted.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Send Elevation (God Client)  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x14** 
Sent By: Client  
Size: 6  

  
**Packet Build**  
BYTE\[1\] Cmd  
BYTE\[2\] Map X Coord  
BYTE\[2\] Map Y Coord  
BYTE\[1\] Z Amount to alter the map tile's elevation by  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Request Script Names  
Last Modified: 2009-03-25 12:46:45  
Modified By: MuadDib  
  
**Packet: 0x16** 
Sent By: Client  
Size: 1  

  
**Packet Build**  
BYTE\[1\] Command  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Control Animation  
Last Modified: 2009-07-26 14:35:27  
Modified By: MuadDib  
  
**Packet: 0x1E** 
Sent By: Client  
Size: 4 bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[3\] Unknown Details  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Get Player Status  
Last Modified: 2009-02-17 20:13:39  
Modified By: MuadDib  
  
**Packet: 0x34** 
Sent By: Client  
Size: 10 bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[4\] Unknown(0xedededed)   
BYTE\[1\] Type   
BYTE\[4\] Mobile Serial  

  
**Subcommand Build**  
N/A

  
**Notes**  
Types:  
0x00: God Client (packet unknown)  
0x04: Basic Status (Packet 0x11)  
0x05: Request Skills (Packet 0x3A)  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Add Resource (God Client)  
Last Modified: 2009-02-17 20:14:52  
Modified By: MuadDib  
  
**Packet: 0x35** 
Sent By: Client  
Size: 653  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[1\] 0 (Unknown)  
BYTE\[1\] 1 (Unknown)  
BYTE\[2\] Tile Type to attach new resource too. If 0x8000+ is for map tiles.  
BYTE\[127\] Internal Resource Name.  
BYTE\[1\] 00 Null Terminator.  
BYTE\[127\] Food Resource Name.  
BYTE\[1\] 00 Null Terminator.  
BYTE\[127\] Shelter Resource Name.  
BYTE\[1\] 00 Null Terminator.  
BYTE\[127\] Desire Resource Name.  
BYTE\[1\] 00 Null Terminator.  
BYTE\[127\] Production Resource Name.  
BYTE\[1\] 00 Null Terminator.  
BYTE\[4\] If set to one, then make a consumer if excess in world.  
BYTE\[4\] If set to one, then make a producer if excess in bank.  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Move Item (God Client)  
Last Modified: 2009-02-17 20:17:31  
Modified By: MuadDib  
  
**Packet: 0x37** 
Sent By: Client  
Size: 8  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[4\] Serial to move  
BYTE\[1\] Z To alter by ?  
BYTE\[1\] Y to alter by ?  
BYTE\[1\] X to alter by ?  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Pathfinding in Client  
Last Modified: 2009-02-17 20:20:54  
Modified By: MuadDib  
  
**Packet: 0x38** 
Sent By: Client  
Size: 7 Bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[2\] X  
BYTE\[2\] Y  
BYTE\[2\] Z  

  
**Subcommand Build**  
N/A

  
**Notes**  
Only works for player played by client.  
x,y,z have to be in range (check packet 0xc8 for range details) from players current location. Packet has to be sent 19 times or more in a row, otherwise it does not do anything, relatively speaking, on OSI servers.  
  
If all this satisfied it does a path finding like action for the character.  
  
location of destination is displayed above players head (cannot be disabled).  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Buy Item(s)  
Last Modified: 2009-03-02 04:23:47  
Modified By: Turley  
  
**Packet: 0x3B** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] blockSize  
BYTE\[4\] vendorID  
BYTE\[1\] flag  
* 0x00 - no items following
* 0x02 - items following
**For each item**  
* BYTE\[1\] (0x1A)
* BYTE\[4\] itemID (from 3C packet)
* BYTE\[2\] # bought

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Version OK  
Last Modified: 2009-08-02 17:17:03  
Modified By: MuadDib  
  
**Packet: 0x45** 
Sent By: Client  
Size: 5 bytes  

  
**Packet Build**  
BYTE\[1\] 0x45  
BYTE\[4\] Unknown  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** New Artwork  
Last Modified: 2009-08-02 17:30:11  
Modified By: MuadDib  
  
**Packet: 0x46** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x46  
BYTE\[2\] Length  
BYTE\[4\] Tile ID  
BYTE\[\*\] Art information (As from art.mul)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** New Terrain  
Last Modified: 2009-08-02 17:34:25  
Modified By: MuadDib  
  
**Packet: 0x47** 
Sent By: Client  
Size: 11 bytes  

  
**Packet Build**  
BYTE\[1\] 0x47  
BYTE\[2\] X  
BYTE\[2\] Y  
BYTE\[2\] Art ID  
BYTE\[2\] Width  
BYTE\[2\] Height  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** New Animation  
Last Modified: 2009-08-02 17:38:50  
Modified By: MuadDib  
  
**Packet: 0x48** 
Sent By: Client  
Size: 73  

  
**Packet Build**  
BYTE\[1\] 0x48  
BYTE\[4\] Tile ID  
BYTE\[64\] Frames  
BYTE\[1\] Unknown  
BYTE\[1\] Frame Count  
BYTE\[1\] Frame Interval  
BYTE\[1\] Start Interval  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** New Hues  
Last Modified: 2009-08-02 17:50:10  
Modified By: MuadDib  
  
**Packet: 0x49** 
Sent By: Client  
Size: 93  

  
**Packet Build**  
BYTE\[1\] 0x49  
BYTE\[4\] Hue ID  
BYTE\[32\] Hue Values  
BYTE\[2\] Start  
BYTE\[2\] End  
BYTE\[20\] Name  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Delete Art  
Last Modified: 2009-08-02 18:37:10  
Modified By: MuadDib  
  
**Packet: 0x4A** 
Sent By: Client  
Size: 5 bytes  

  
**Packet Build**  
BYTE\[1\] 0x4A  
BYTE\[4\] Art ID  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Check Client Version  
Last Modified: 2009-08-02 18:41:48  
Modified By: MuadDib  
  
**Packet: 0x4B** 
Sent By: Client  
Size: 9 bytes  

  
**Packet Build**  
BYTE\[1\] 0x4B  
BYTE\[8\] Unknown  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Script Names  
Last Modified: 2009-08-02 19:42:05  
Modified By: MuadDib  
  
**Packet: 0x4C** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x4C  
BYTE\[2\] Length  
BYTE\[Length-3\] Data  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Edit Script File  
Last Modified: 2009-08-02 18:58:05  
Modified By: MuadDib  
  
**Packet: 0x4D** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x4D  
BYTE\[2\] Length  
BYTE\[Length-3\] Data  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Board Header  
Last Modified: 2009-08-02 19:05:36  
Modified By: MuadDib  
  
**Packet: 0x50** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x50  
BYTE\[2\] Length  
BYTE\[Length-3\] Data  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Board Message  
Last Modified: 2009-08-02 19:07:42  
Modified By: MuadDib  
  
**Packet: 0x51** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x51  
BYTE\[2\] Length  
BYTE\[Length-3\] Data  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Board Post Message  
Last Modified: 2009-08-02 19:08:08  
Modified By: MuadDib  
  
**Packet: 0x52** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x52  
BYTE\[2\] Length  
BYTE\[Length-3\] Data  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Update Regions  
Last Modified: 2009-08-02 19:10:52  
Modified By: MuadDib  
  
**Packet: 0x57** 
Sent By: Client  
Size: 110  

  
**Packet Build**  
BYTE\[1\] 0x57  
BYTE\[109\] Unknown  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Add Region  
Last Modified: 2009-08-02 19:25:03  
Modified By: MuadDib  
  
**Packet: 0x58** 
Sent By: Client  
Size: 106  

  
**Packet Build**  
BYTE\[1\]  
BYTE\[40\] Name  
BYTE\[4\] 0x0000  
BYTE\[2\] X  
BYTE\[2\] Y  
BYTE\[2\] Width;  
BYTE\[2\] Height;  
BYTE\[2\] Z 1;  
BYTE\[2\] Z 2;  
BYTE\[40\] Description  
BYTE\[2\] Sound Effect  
BYTE\[2\] Music (Enter/Leave?)  
BYTE\[2\] Sound Effect Night Time  
BYTE\[1\] Dungeon 0/1  
BYTE\[2\] Light Level  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** New Context FX  
Last Modified: 2009-08-02 19:42:49  
Modified By: MuadDib  
  
**Packet: 0x59** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x59  
BYTE\[2\] Length  
BYTE\[Length-3\] Data  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Update Context FX  
Last Modified: 2009-08-02 19:43:40  
Modified By: MuadDib  
  
**Packet: 0x5A** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x59  
BYTE\[2\] Length  
BYTE\[Length-3\] Data  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Restart Version  
Last Modified: 2009-08-02 19:45:47  
Modified By: MuadDib  
  
**Packet: 0x5C** 
Sent By: Client  
Size: 2 bytes  

  
**Packet Build**  
BYTE\[1\] 0x5C  
BYTE\[1\] Unknown  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Login Character  
Last Modified: 2009-08-20 01:33:41  
Modified By: Turley  
  
**Packet: 0x5D** 
Sent By: Client  
Size: 73 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] pattern1 (0xedededed)  
BYTE\[30\] char name (0 terminated)  
BYTE\[2\] unknown0  
BYTE\[4\] clientflag (see notes)  
BYTE\[4\] unknown1  
BYTE\[4\] logincount  
BYTE\[16\] unknown2  
BYTE\[4\] slot choosen  
BYTE\[4\] clientIP  

  
**Subcommand Build**  
N/A

  
**Notes**  
clientflag:  
t2a: 0x00,  
renaissance: 0x01,  
third dawn: 0x02,  
lbr: 0x04,  
aos: 0x08,  
se: 0x10,  
sa: 0x20,  
uo3d: 0x40,  
reserved: 0x80,  
3dclient: 0x100  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Server Listing  
Last Modified: 2009-08-02 19:47:38  
Modified By: MuadDib  
  
**Packet: 0x5E** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x5E  
BYTE\[2\] Length  
BYTE\[Length-3\] Data  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Server List Add Entry  
Last Modified: 2009-08-02 19:49:10  
Modified By: MuadDib  
  
**Packet: 0x5F** 
Sent By: Client  
Size: 49  

  
**Packet Build**  
BYTE\[1\] 0x5F  
BYTE\[48\] Data  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Server List Remove Entry  
Last Modified: 2009-08-02 19:55:15  
Modified By: MuadDib  
  
**Packet: 0x60** 
Sent By: Client  
Size: 5 bytes  

  
**Packet Build**  
BYTE\[1\] 0x60  
BYTE\[4\] Entry ID  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Remove Static Object  
Last Modified: 2009-08-02 20:00:18  
Modified By: MuadDib  
  
**Packet: 0x61** 
Sent By: Client  
Size: 9 bytes  

  
**Packet Build**  
BYTE\[1\] 0x61  
BYTE\[2\] X  
BYTE\[2\] Y  
BYTE\[2\] Z  
BYTE\[2\] Static Object ID  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Move Static Object  
Last Modified: 2009-08-02 20:02:51  
Modified By: MuadDib  
  
**Packet: 0x62** 
Sent By: Client  
Size: 15 bytes  

  
**Packet Build**  
BYTE\[1\] 0x62  
BYTE\[1\] Old X  
BYTE\[1\] Old Y  
BYTE\[1\] Old Z  
BYTE\[1\] Item ID  
BYTE\[1\] Z Offset  
BYTE\[1\] Y Offset  
BYTE\[1\] X Offset  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Load Area  
Last Modified: 2009-08-02 20:05:59  
Modified By: MuadDib  
  
**Packet: 0x63** 
Sent By: Client  
Size: 13 bytes  

  
**Packet Build**  
BYTE\[1\] 0x63  
BYTE\[12\] Unknown  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Load Area Request  
Last Modified: 2009-08-02 20:06:31  
Modified By: MuadDib  
  
**Packet: 0x64** 
Sent By: Client  
Size: 1 byte  

  
**Packet Build**  
BYTE\[1\] 0x64  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Change Text/Emote Colors  
Last Modified: 2009-03-02 05:09:03  
Modified By: Turley  
  
**Packet: 0x69** 
Sent By: Client  
Size: 5 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] blockSize  
BYTE\[2\] unknown1  

  
**Subcommand Build**  
N/A

  
**Notes**  
The client sends two of these independent of the color chosen. It sends two of them in quick succession as part of the "same" packet. The unknown1 is 0x00 0x01 in the first and 0x00 0x02 in the second.  
  
This message has been removed. It is no longer used.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Rename Character  
Last Modified: 2009-03-02 05:09:22  
Modified By: Turley  
  
**Packet: 0x75** 
Sent By: Client  
Size: 35 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] id  
BYTE\[30\] new name  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Response To Dialog Box  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x7D** 
Sent By: Client  
Size: 13 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[4\] dialogID (echoed back from 7C packet)   
BYTE\[2\] menuid (echoed back from 7C packet)   
BYTE\[2\] 1-based index of choice   
BYTE\[2\] model # of choice   
BYTE\[2\] color  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Login Request  
Last Modified: 2009-02-28 15:42:51  
Modified By: MuadDib  
  
**Packet: 0x80** 
Sent By: Client  
Size: 62 Bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[30\] Account Name  
BYTE\[30\] Password  
BYTE\[1\] NextLoginKey value from uo.cfg on client machine.  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Delete Character  
Last Modified: 2009-03-02 05:09:36  
Modified By: Turley  
  
**Packet: 0x83** 
Sent By: Client  
Size: 39 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[30\] password  
BYTE\[4\] charIndex  
BYTE\[4\] clientIP  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Character Creation ( KR + SA 3D clients only )  
Last Modified: 2010-01-09 13:21:36  
Modified By: Tomi  
  
**Packet: 0x8D** 
Sent By: Client  
Size: 146 bytes  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[2\] packet length   
BYTE\[4\] pattern1 (0xedededed)   
BYTE\[4\] character index   
BYTE\[30\] character name   
BYTE\[30\] unknown (character password?)  
BYTE\[1\] profession  
BYTE\[1\] client flags   
BYTE\[1\] gender (male=0, female=1)   
BYTE\[1\] race (human=0, elf=1, gargoyle=2)  
BYTE\[1\] strength   
BYTE\[1\] dexterity   
BYTE\[1\] intelligence   
BYTE\[2\] skin color   
BYTE\[4\] unknown (0)   
BYTE\[4\] unknown (0)   
BYTE\[1\] skill1   
BYTE\[1\] skill1 value   
BYTE\[1\] skill2   
BYTE\[1\] skill2 value   
BYTE\[1\] skill3   
BYTE\[1\] skill3 value   
BYTE\[1\] skill4   
BYTE\[1\] skill4 value   
BYTE\[25\] unknown (0)   
BYTE\[1\] unknown (0x0B)   
BYTE\[2\] hair color   
BYTE\[2\] hair style   
BYTE\[1\] unknown (0x0C)   
BYTE\[4\] unknown (0)   
BYTE\[1\] unknown (0x0D)   
BYTE\[2\] shirt color   
BYTE\[2\] shirt style  
BYTE\[1\] unknown (0x0F)   
BYTE\[2\] face color   
BYTE\[2\] face style  
BYTE\[1\] unknown (0x10)   
BYTE\[2\] beard style   
BYTE\[2\] beard color  

  
**Subcommand Build**  
N/A

  
**Notes**  
KR clients send this packet on character creation.  
Client Flags = 0x41 or 0x3F? Possibly there is a need to use the  
offset value from packet 0xE1.  
This packet needs to be tested and verified!!  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Game Server Login  
Last Modified: 2009-03-02 05:09:49  
Modified By: Turley  
  
**Packet: 0x91** 
Sent By: Client  
Size: 65 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] key used  
BYTE\[30\] sid  
BYTE\[30\] password  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Move Player  
Last Modified: 2009-08-04 13:33:37  
Modified By: MuadDib  
  
**Packet: 0x97** 
Sent By: Client  
Size: 2 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] direction  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Request Help  
Last Modified: 2009-03-02 05:10:02  
Modified By: Turley  
  
**Packet: 0x9B** 
Sent By: Client  
Size: 258 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[257\] (0x00)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Sell List Reply  
Last Modified: 2009-03-02 04:25:59  
Modified By: Turley  
  
**Packet: 0x9F** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] blockSize  
BYTE\[4\] shopkeeperID  
BYTE\[2\] itemCount  
**For each item, a structure containing:**  
* BYTE\[4\] itemID
* BYTE\[2\] quantity

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Select Server  
Last Modified: 2009-03-25 12:45:02  
Modified By: MuadDib  
  
**Packet: 0xA0** 
Sent By: Client  
Size: 3 Bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[2\] Shard Chosen from Server List  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Client Spy  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0xA4** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[148\] Unknown (previously, this has had info such as your graphics card name, free HD space, number of processors, etc.)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Request Tip/Notice Window  
Last Modified: 2009-03-02 04:26:46  
Modified By: Turley  
  
**Packet: 0xA7** 
Sent By: Client  
Size: 4 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] last tip #  
BYTE\[1\] flag  
* 0x00 - tips window
* 0x01 - notice window

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Gump Text Entry Dialog Reply  
Last Modified: 2009-03-02 05:10:33  
Modified By: Turley  
  
**Packet: 0xAC** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
BYTE\[4\] ID  
BYTE\[1\] type  
BYTE\[1\] index  
BYTE\[3\] unkown?  
BYTE\[?\] reply  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Unicode/Ascii speech request  
Last Modified: 2009-03-02 04:30:44  
Modified By: Turley  
  
**Packet: 0xAD** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
BYTE\[1\] Type   
BYTE\[2\] Color   
BYTE\[2\] Font   
BYTE\[4\] Language (Null Terminated)  
* "enu" - United States English
* "des" - German Swiss
* "dea" - German Austria
* "deu" - German Germany
* ... for a complete list see langcode.iff
**if (Type & 0xc0)**   
* BYTE\[1,5\] Number of distinct Trigger words (NUMWORDS). 12 Bit number, Byte #13 = Bit 11..4 of NUMWORDS, Hi-Nibble of Byte #14 (Bit 7..4) = Bit 0..3 of NUMWORDS
* BYTE\[1,5\] Index to speech.mul. 12 Bit number, Low Nibble of Byte #14 (Bits ..0) = Bits 11..8 of Index, Byte #15 = Bits 7..0 of Index
* UNKNOWNS = ( (NUMWORDS div 2) \*3 ) + (NUMWORDS % 2) - 1\. div = Integeger division, % = modulo operation, NUMWORDS >= 1\. examples: UNKNOWNS(1)=0, UNKNOWNS(2)=2, UNKNOWNS(3)=3, UNKNOWNS(4)=5, UNKNOWNS(5)=6, UNKNOWNS(6)=8, UNKNOWNS(7)=9, UNKNOWNS(8)=11, UNKNOWNS(9)=12
* BYTE\[UNKNOWNS\] Idea behind this is getting speech parsing load client side.
Thus this contains data OSI server use for easier parsing. It's client side hardcoded and exact details are unkown.  
* BYTE\[?\] Ascii Msg - Null Terminated(blockSize - (15+UNKNOWNS) )
else   
* BYTE\[?\]\[2\] Unicode Msg - Null Terminated (blockSize - 12)

  
**Subcommand Build**  
N/A

  
**Notes**  
For pre 2.0.7 clients Type is always < 0xc0\. Uox based emus convert post 2.0.7 data of this packet to pre 2.0.7 data if Type >=0xc0.  
  
(different view of it)  
If Mode&0xc0 then there are keywords (from speech.mul) present.   
Keywords:  
The first 12 bits = the number of keywords present. The keywords are included right after this, each one is 12 bits also.   
The keywords are padded to the closest byte. For example, if there are 2 keywords, it will take up 5 bytes. 12bits for the number, and 12 bits for each keyword. 12+12+12=36\. Which will be padded 4 bits to 40 bits or 5 bytes.  
  
The various types of text is as follows:  
0x00 - Normal   
0x01 - Broadcast/System  
0x02 - Emote   
0x06 - System/Lower Corner  
0x07 - Message/Corner With Name  
0x08 - Whisper  
0x09 - Yell  
0x0A - Spell  
0x0D - Guild Chat  
0x0E - Alliance Chat  
0x0F - Command Prompts  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Gump Menu Selection  
Last Modified: 2009-03-02 04:32:14  
Modified By: Turley  
  
**Packet: 0xB1** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[2\] length  
BYTE\[4\] id (first Id in 0xb0)  
BYTE\[4\] gumpId (second Id in 0xb0)  
BYTE\[4\] buttonId (which button perssed ? 0 if closed)  
BYTE\[4\] switchcount (response info for radio buttons and checkboxes, any switches listed here are switched on)  
**For each switch**  
* BYTE\[4\] SwitchId
BYTE\[4\] textcount (response info for textentries)  
**For each textentry**  
* BYTE\[2\] textId
* BYTE\[2\] textlength
* BYTE\[length\*2\] Unicode text (not nullterminated)

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Open Chat Window  
Last Modified: 2009-03-02 05:10:50  
Modified By: Turley  
  
**Packet: 0xB5** 
Sent By: Client  
Size: 64 bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[63\] chatname, if known by client (all 00 if unknown) (name in unicode)  

  
**Subcommand Build**  
N/A

  
**Notes**  
This message is very incomplete. From the server, just know that it is 0xB5 len len, and pass the data through as is appropriate.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Send Help/Tip Request  
Last Modified: 2009-03-02 04:32:56  
Modified By: Turley  
  
**Packet: 0xB6** 
Sent By: Client  
Size: 9 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] id  
BYTE\[1\] language # (1 for enu)  
BYTE\[3\] language name (enu for English - United states)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Invalid Map (Request?)  
Last Modified: 2009-03-25 12:51:25  
Modified By: MuadDib  
  
**Packet: 0xC5** 
Sent By: Client  
Size: 1 Byte  

  
**Packet Build**  
BYTE\[1\] Command  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Spy On Client  
Last Modified: 2009-03-02 05:11:13  
Modified By: Turley  
  
**Packet: 0xD9** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] unknown (02) Always 0x02 in my tests  
BYTE\[4\] Unique Instance ID of UO  
BYTE\[4\] OS Major  
BYTE\[4\] OS Minor  
BYTE\[4\] OS Revision  
BYTE\[1\] CPU Manufacturer  
BYTE\[4\] CPU Family  
BYTE\[4\] CPU Model   
BYTE\[4\] CPU Clock Speed  
BYTE\[1\] CPU Quantity  
BYTE\[4\] Memory  
BYTE\[4\] Screen Width  
BYTE\[4\] Screen Height  
BYTE\[4\] Screen Depth  
BYTE\[2\] Direct X Version  
BYTE\[2\] Direct X Minor  
BYTE\[76?\] Video Card Description  
BYTE\[4\] Video Card Vendor ID  
BYTE\[4\] Video Card Device ID   
BYTE\[4\] Video Card Memory  
BYTE\[1\] Distribution  
BYTE\[1\] Clients Running  
BYTE\[1\] Clients Installed  
BYTE\[1\] Partial Insstalled  
BYTE\[1\] Unknown  
BYTE\[4\] Language Code  
BYTE\[67\] Unknown Ending  

  
**Subcommand Build**  
N/A

  
**Notes**  
Older clients, from mid-late 2003 was set at 149 bytes length, whereas newest clients 4.01+ are set to 199 ??  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Bug Report (KR)  
Last Modified: 2009-03-02 04:33:47  
Modified By: Turley  
  
**Packet: 0xE0** 
Sent By: Client  
Size: variable  

  
**Packet Build**  
BYTE\[1\] CMD  
BYTE\[2\] packet length  
BYTE\[3\] unicode language  
BYTE\[1\] unknown (0x00)  
BYTE\[2\] Bug category (see notes)  
Byte\[\*\] Bug Description in Unicode  

  
**Subcommand Build**  
N/A

  
**Notes**  
Bug categories:  
0x01 - World Environment  
0x02 - Wearables  
0x03 - Combat  
0x04 - UI  
0x05 - Crash  
0x06 - Stuck  
0x07 - Animations  
0x08 - Performance  
0x09 - NPCs  
0x0A - Creatures  
0x0B - Pets  
0x0C - Housing  
0x0D - Lost Item  
0x0E - Exploit  
0x0F - Other  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Client Type (KR/SA)  
Last Modified: 2009-08-13 07:00:20  
Modified By: Tomi  
  
**Packet: 0xE1** 
Sent By: Client  
Size: 9 bytes  

  
**Packet Build**  
BYTE\[1\] CMD  
BYTE\[2\] packet length  
BYTE\[2\] unknown (0x0001)  
BYTE\[4\] cienttype  

  
**Subcommand Build**  
N/A

  
**Notes**  
clienttype:  
  
0x2: KR  
0x3: SA  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Equip Macro (KR)  
Last Modified: 2009-03-02 04:35:14  
Modified By: Turley  
  
**Packet: 0xEC** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] CMD  
BYTE\[2\] Length  
BYTE\[2\] Item Count  
Item Segments:  
* BYTE\[4\] Item Serial

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Unequip Item Macro (KR)  
Last Modified: 2009-03-02 04:35:39  
Modified By: Turley  
  
**Packet: 0xED** 
Sent By: Client  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] CMD  
BYTE\[2\] Length  
BYTE\[1\] Layer Count  
Layer Segments:  
* BYTE\[1\] Layer ID

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** KR/2D Client Login/Seed  
Last Modified: 2008-12-16 07:57:46  
Modified By: Pierce  
  
**Packet: 0xEF** 
Sent By: Client  
Size: 21 bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] seed, usually the client local ip  
BYTE\[4\] client major version  
BYTE\[4\] client minor version  
BYTE\[4\] client revision version  
BYTE\[4\] client prototype version  

  
**Subcommand Build**  
N/A

  
**Notes**  
Normally older client send a 4 byte seed (local ip).  
Newer clients 2.48.0.3+ (KR) and 6.0.5.0+ (2D) are sending  
this packet.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Damage  
Last Modified: 2009-08-06 03:19:54  
Modified By: Turley  
  
**Packet: 0x0B** 
Sent By: Server  
Size: 7 bytes  

  
**Packet Build**  
BYTE\[1\] 0x0B  
BYTE\[4\] Serial to display above  
BYTE\[2\] Damage dealt. 0xFFFF is max damage that can be displayed, duh!  

  
**Subcommand Build**  
N/A

  
**Notes**  
New damage dealt packet to display damage above a mob's head. This packet was introduced somewhere around 4.0.7a  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Status Bar Info  
Last Modified: 2009-08-13 10:43:24  
Modified By: MuadDib  
  
**Packet: 0x11** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x11  
BYTE\[2\] Length  
BYTE\[4\] Player Serial  
BYTE\[30\] Player Name  
BYTE\[2\] Current Hit Points (see notes)  
BYTE\[2\] Max Hit Points (see notes)  
BYTE\[1\] Name Change Flag (see notes)  
BYTE\[1\] Status Flag (see notes)  
BYTE\[1\] Sex+Race (see notes)  
BYTE\[2\] Strength  
BYTE\[2\] Dexterity  
BYTE\[2\] Intelligence  
BYTE\[2\] Current Stamina  
BYTE\[2\] Max Stamina  
BYTE\[2\] Current Mana  
BYTE\[2\] Max Mana  
BYTE\[4\] Gold In Pack  
BYTE\[2\] Armor Rating (see notes)  
BYTE\[2\] Weight  
**If (flag 5 or higher)**  
* BYTE\[2\] Max Weight
* BYTE\[1\] Race (see notes)
**If (flag 3 or higher )**  
* BYTE\[2\] Stats Cap
* BYTE\[1\] Followers (Pets)
* BYTE\[1\] Followers Max Possible (Pets)
**If (flag 4 or higher)**  
* BYTE\[2\] Fire Resist (see notes)
* BYTE\[2\] Cold Resist (see notes)
* BYTE\[2\] Poison Resist (see notes)
* BYTE\[2\] Energy Resist (see notes)
* BYTE\[2\] Luck
* BYTE\[2\] Damage Minimum
* BYTE\[2\] Damage Maximum
* BYTE\[4\] Tithing points (Paladin Books)
**If (flag 6 or higher)**  
* BYTE\[2\] Hit Chance Increase
* BYTE\[2\] Swing Speed Increase
* BYTE\[2\] Damage Chance Increase
* BYTE\[2\] Lower Reagent Cost
* BYTE\[2\] Hit Points Regeneration
* BYTE\[2\] Stamina Regeneration
* BYTE\[2\] Mana Regeneration
* BYTE\[2\] Reflect Physical Damage
* BYTE\[2\] Enhance Potions
* BYTE\[2\] Defense Chance Increase
* BYTE\[2\] Spell Damage Increase
* BYTE\[2\] Faster Cast Recovery
* BYTE\[2\] Faster Casting
* BYTE\[2\] Lower Mana Cost
* BYTE\[2\] Strength Increase
* BYTE\[2\] Dexterity Increase
* BYTE\[2\] Intelligence Increase
* BYTE\[2\] Hit Points Increase
* BYTE\[2\] Stamina Increase
* BYTE\[2\] Mana Increase
* BYTE\[2\] Maximum Hit Points Increase
* BYTE\[2\] Maximum Stamina Increase
* BYTE\[2\] Maximum Mana Increase

  
**Subcommand Build**  
N/A

  
**Notes**  
For characters other than the player, Current Hitpoints and Max Hitpoints are not the actual values. Max Hitpoints is a fixed value, and Current Hitpoints works like a percentage. This is to assist in blocking injection style tools from seeing real hitpoint values.  
  
Name Change Flag  
* 1: Allowed to Change In StatusBar (like with pets)
* 0: Not allowed
  
Status Flag  
* 0x00: no more data following (end of packet here).
* 0x01: T2A Extended Info
* 0x03: UOR Extended Info
* 0x04: AOS Extended Info (4.0+)
* 0x05: UOML Extended Info (5.0+)
* 0x06: UOKR Extended Info (UOKR+)
  
Sex + Race  
* 0: Male Human
* 1: Female Human
* 2: Male Elf
* 3: Female Elf
  
Armor Rating  
Armor Rating depends on client settings. If client has AOS Resistances enabled, this should be the Physical Resist instead of older AR Rating.  
  
UOML+ Race Flag  
* 1: Human
* 2: Elf
* 3: Gargoyle
  
Resistances  
Resistances can be negatives. Easiest method for handling this correctly is, if a negative is to be sent is: 0x10000+amount  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Health bar status update (KR)  
Last Modified: 2009-07-28 14:21:26  
Modified By: MuadDib  
  
**Packet: 0x17** 
Sent By: Server  
Size: 12 bytes  

  
**Packet Build**  
BYTE\[1\] 0x17  
BYTE\[2\] Length  
BYTE\[4\] Mobile Serial   
BYTE\[2\] 0x0001  
BYTE\[2\] HealthBar Color (1=green, 2=yellow, >2=red?)  
BYTE\[1\] Flag (0=Remove health bar color, 1=Enable health bar color)  

  
**Subcommand Build**  
N/A

  
**Notes**  
This packet is send to both 2D and KR client.  
When mobile is poisoned (green health bar), flag is determined as:  
0: remove poison, 1: poison level  
  
Green (Poison) overrides yellow. If both are set yellow appears only if green is disabled.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Object Info  
Last Modified: 2009-07-28 14:49:24  
Modified By: MuadDib  
  
**Packet: 0x1A** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] 0x1A  
BYTE\[2\] Length  
BYTE\[4\] Object ID   
BYTE\[2\] Graphic  
**If (Object ID& 0x80000000)**  
* BYTE\[2\] item count (or Graphic for corpses)
**If (Graphic & 0x8000)**   
* BYTE Increment Counter (increment graphic by this #)
BYTE\[2\] xLoc (only use lowest significant 15 bits)   
BYTE\[2\] yLoc   
**If (xLoc & 0x8000)**   
* BYTE direction
BYTE zLoc   
**If (yLoc & 0x8000)**   
* BYTE\[2\] dye
**If (yLoc & 0x4000)**  
* BYTE\[1\] Flag

  
**Subcommand Build**  
N/A

  
**Notes**  
Flag:  
* 0x00 - None
* 0x02 - Female
* 0x04 - Poisoned
* 0x08 - YellowHits // healthbar gets yellow
* 0x10 - FactionShip // unsure why client needs to know
* 0x20 - Movable if normally not
* 0x40 - War mode
* 0x80 - Hidden

  
---

  
[\[^\]](#TOP)

**Packet Name:** Char Locale and Body  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x1B** 
Sent By: Server  
Size: 37 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[4\] player serial  
BYTE\[4\] unknown. always 0  
BYTE\[2\] body type  
BYTE\[2\] xLoc   
BYTE\[2\] yLoc   
BYTE\[1\] Unknown 0  
BYTE\[1\] zLoc   
BYTE\[1\] facing  
BYTE\[4\] unknown 0x0  
BYTE\[4\] unknown 0x0  
BYTE\[1\] unknown 0x0  
BYTE\[2\] server boundary width minus eight.  
BYTE\[2\] server boundary Height  
BYTE\[2\] unknown 0x0  
BYTE\[4\] unknown 0x0  

  
**Subcommand Build**  
N/A

  
**Notes**  
Also known as "Login Confirmation". This packet must be sent by the server once during login in order to confirm the login happened fully.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Send Speech  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x1C** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[2\] Packet len  
BYTE\[4\] itemID (FF FF FF FF = system)   
BYTE\[2\] model (item # - FF FF = system)   
BYTE\[1\] Type of Text  
BYTE\[2\] Text Color   
BYTE\[2\] Font   
BYTE\[30\] Name   
BYTE\[?\] Null-Terminated Msg (? = Packet length - 44)  

  
**Subcommand Build**  
N/A

  
**Notes**  
This is the packet POL uses to send names when you single click on items or NPCs. This is a much more optimized method (packet size) than OSI's method by using 0xC1  
  
The various types of text is as follows:  
0x00 - Normal   
0x01 - Broadcast/System  
0x02 - Emote   
0x06 - System/Lower Corner  
0x07 - Message/Corner With Name  
0x08 - Whisper  
0x09 - Yell  
0x0A - Spell  
0x0D - Guild Chat  
0x0E - Alliance Chat  
0x0F - Command Prompts  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Delete Object  
Last Modified: 2009-03-02 05:11:38  
Modified By: Turley  
  
**Packet: 0x1D** 
Sent By: Server  
Size: 5 bytes  

  
**Packet Build**  
BYTE cmd  
BYTE\[4\] item/char id  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Explosion  
Last Modified: 2009-07-26 14:34:04  
Modified By: MuadDib  
  
**Packet: 0x1F** 
Sent By: Server  
Size: 8 bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[7\] Unknown details  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Draw Game Player  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x20** 
Sent By: Server  
Size: 19 bytes  

  
**Packet Build**  
BYTE cmd  
BYTE\[4\] creature id  
BYTE\[2\] bodyType  
BYTE\[1\] unknown1 (0)  
BYTE\[2\] skin color / hue  
BYTE\[1\] flag byte  
BYTE\[2\] xLoc  
BYTE\[2\] yLoc  
BYTE\[2\] unknown2 (0)  
BYTE\[1\] direction  
BYTE\[1\] zLoc  

  
**Subcommand Build**  
N/A

  
**Notes**  
Only used with the character being played by the client.  
  
01-10-2007  
Interesting, 0x12 Status Flag was a guild mates mount not in war mode.  
0x52 is for the same pets status flag while in war mode. Poisoned it stayed the same appropriately in each. 0x12 and 0x52  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Char Move Rejection  
Last Modified: 2009-03-02 05:11:57  
Modified By: Turley  
  
**Packet: 0x21** 
Sent By: Server  
Size: 8 bytes  

  
**Packet Build**  
BYTE cmd  
BYTE\[1\] sequence #  
BYTE\[2\] xLoc  
BYTE\[2\] yLoc  
BYTE\[1\] direction  
BYTE\[1\] zLoc  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Dragging Of Item  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x23** 
Sent By: Server  
Size: 26 bytes  

  
**Packet Build**  
BYTE\[1\] Cmd  
BYTE\[2\] model #  
BYTE\[3\] unknown1  
BYTE\[2\] stack count  
BYTE\[4\] Source ID  
BYTE\[2\] Source xLoc  
BYTE\[2\] Source yLoc  
BYTE\[1\] Source zLoc  
BYTE\[4\] Target id  
BYTE\[2\] Target xLoc  
BYTE\[2\] Target yLoc  
BYTE\[1\] Target zLoc  

  
**Subcommand Build**  
N/A

  
**Notes**  
This does not actually move the item, it just displays an animation.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Draw Container  
Last Modified: 2009-03-02 05:12:21  
Modified By: Turley  
  
**Packet: 0x24** 
Sent By: Server  
Size: 7 bytes  

  
**Packet Build**  
BYTE cmd  
BYTE\[4\] item id   
BYTE\[2\] model-Gump (0x003c is backpack)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Add Item To Container  
Last Modified: 2009-08-13 11:06:19  
Modified By: MuadDib  
  
**Packet: 0x25** 
Sent By: Server  
Size: 20/21 bytes  

  
**Packet Build**  
2D Client Versions (before 6.0.1.7)  
BYTE\[1\] 0x25  
BYTE\[4\] Item Serial  
BYTE\[2\] Item ID (Graphic)  
BYTE\[1\] Item ID Offset (wtf?)  
BYTE\[2\] Stack Amount  
BYTE\[2\] X  
BYTE\[2\] Y  
BYTE\[4\] Container Serial  
BYTE\[2\] Color  
  
UOKR+ and 2D Client Versions (after 6.0.1.7)  
BYTE\[1\] 0x25  
BYTE\[4\] Item Serial  
BYTE\[2\] Item ID (Graphic)  
BYTE\[1\] Item ID Offset (wtf?)  
BYTE\[2\] Stack Amount  
BYTE\[2\] X  
BYTE\[2\] Y  
BYTE\[1\] Slot Index (see notes)  
BYTE\[4\] Container Serial  
BYTE\[2\] Color  

  
**Subcommand Build**  
N/A

  
**Notes**  
The backpack grid index exists since 6.0.1.7 2D and 2.45.5.6 KR.  
For older clients server has to send 20 bytes without this grid index byte!!  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Kick Player  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x26** 
Sent By: Server  
Size: 5 bytes  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[4\] ID of GM who issued kick?  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Reject Move Item Request  
Last Modified: 2009-02-17 20:00:20  
Modified By: MuadDib  
  
**Packet: 0x27** 
Sent By: Server  
Size: 2 bytes  

  
**Packet Build**  
BYTE\[1\] Cmd  
BYTE\[1\] Reason (0x00)  

  
**Subcommand Build**  
N/A

  
**Notes**  
Known Reason Bytes:  
0x0: Cannot lift the item  
0x1: Out of range  
0x2: Out of sight  
0x3: Belongs to another  
0x4: Already holding something  
0x5: empty message on client?  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Drop Item Failed/Clear Square (God Client?)  
Last Modified: 2009-02-17 20:05:29  
Modified By: MuadDib  
  
**Packet: 0x28** 
Sent By: Server  
Size: 5 bytes  

  
**Packet Build**  
Byte\[1\] Command  
Byte\[4\] Serial of Item  

  
**Subcommand Build**  
N/A

  
**Notes**  
This is an older god client packet also according to Jerrith's original guide. Below is the packet info for history:  
  
BYTE\[1\] cmd  
BYTE\[2\] xLoc   
BYTE\[2\] yLoc  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Drop Item Approved  
Last Modified: 2009-07-21 22:10:24  
Modified By: MuadDib  
  
**Packet: 0x29** 
Sent By: Server  
Size: 1 byte  

  
**Packet Build**  
BYTE\[1\] Cmd  

  
**Subcommand Build**  
N/A

  
**Notes**  
Older 2D Clients this was for Paperdoll ackknowledgement.  
  
Newest (KR era and 6.0.1.7 and higher) use this also for the regular drop item packet.  
  
\> 6.0.1.7 = Client sends Drop On Paperdoll (0x13), Server Responds with This (0x29)  
< 6.0.1.7 = Client sends Drop On Paperdoll (0x13) or sends Drop Item (0x08), server responds with this (0x29)  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Blood  
Last Modified: 2009-02-17 20:07:53  
Modified By: MuadDib  
  
**Packet: 0x2A** 
Sent By: Server  
Size: 5 bytes  

  
**Packet Build**  
Byte\[1\] Command  
Byte\[4\] Serial  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** God Mode (God Client)  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x2B** 
Sent By: Server  
Size: 2  

  
**Packet Build**  
BYTE\[1\] Cmd  
BYTE\[1\] 0/1 For Disable/Enable God Mode in client.  

  
**Subcommand Build**  
N/A

  
**Notes**  
Send this packet after receiving a Request God Mode packet.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Mob Attributes  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x2D** 
Sent By: Server  
Size: 17 bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] serial  
BYTE\[2\] hitsMax  
BYTE\[2\] hitsCurrent  
BYTE\[2\] manaMax  
BYTE\[2\] manaCurrent  
BYTE\[2\] stamMax  
BYTE\[2\] stamCurrent  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Worn Item  
Last Modified: 2009-03-02 05:12:43  
Modified By: Turley  
  
**Packet: 0x2E** 
Sent By: Server  
Size: 15 bytes  

  
**Packet Build**  
BYTE cmd   
BYTE\[4\] itemid (always starts 0x40 in my data)   
BYTE\[2\] model (item hex #)   
BYTE\[1\] (0x00)   
BYTE\[1\] layer  
BYTE\[4\] playerID   
BYTE\[2\] color/hue  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Fight Occuring  
Last Modified: 2009-03-18 21:44:41  
Modified By: MuadDib  
  
**Packet: 0x2F** 
Sent By: Server  
Size: 10 bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[1\] Unknown 0x0  
BYTE\[4\] ID of attacker   
BYTE\[4\] ID of attacked  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Attack Ok  
Last Modified: 2009-03-18 21:45:17  
Modified By: MuadDib  
  
**Packet: 0x30** 
Sent By: Server  
Size: 5  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[4\] Serial to attack  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Attack Ended  
Last Modified: 2009-07-26 15:06:38  
Modified By: MuadDib  
  
**Packet: 0x31** 
Sent By: Server  
Size: 1 byte  

  
**Packet Build**  
BYTE\[1\] Command  

  
**Subcommand Build**  
N/A

  
**Notes**  
No longer in use. Old client packet  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Unknown  
Last Modified: 2009-07-26 15:32:26  
Modified By: MuadDib  
  
**Packet: 0x32** 
Sent By: Server  
Size: 2 Bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[1\] 0x0 (Unknown)  

  
**Subcommand Build**  
N/A

  
**Notes**  
Most claim this is a god mode packet, but is seen with regular clients on EA Servers. Need more information to confirm  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Pause Client  
Last Modified: 2009-02-17 20:12:04  
Modified By: MuadDib  
  
**Packet: 0x33** 
Sent By: Server  
Size: 2 bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[1\] pause/resume (1=pause, 0=resume)  

  
**Subcommand Build**  
N/A

  
**Notes**  
Unpause does not seem to work.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Resource Tile Data (God Client  
Last Modified: 2009-02-17 20:16:36  
Modified By: MuadDib  
  
**Packet: 0x36** 
Sent By: Server  
Size: Unknown  

  
**Packet Build**  
Byte\[1\] Command  
Byte\[2\] Length  
Byte\[\] Unknown  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Add multiple Items In Container  
Last Modified: 2009-03-02 05:15:23  
Modified By: Turley  
  
**Packet: 0x3C** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[2\] packet length   
BYTE\[2\] number of Items to add   
loop items:   
* BYTE\[4\] item serial
* BYTE\[2\] item ID (objtype)
* BYTE\[1\] unknown (0x00)
* BYTE\[2\] item amount (stack)
* BYTE\[2\] xLoc
* BYTE\[2\] yLoc
* BYTE\[1\] Backpack grid index (see notes)
* BYTE\[4\] Container serial
* BYTE\[2\] item color
endloop  

  
**Subcommand Build**  
N/A

  
**Notes**  
The backpack grid index exists since 6.0.1.7 2D and 2.45.5.6 KR.  
For older clients server has to send the loop without this grid index byte:  
  
loop items:  
* BYTE\[4\] item serial
* BYTE\[2\] item ID (objtype)
* BYTE\[1\] unknown (0x00)
* BYTE\[2\] item amount (stack)
* BYTE\[2\] xLoc
* BYTE\[2\] yLoc
* BYTE\[4\] Container serial
* BYTE\[2\] item color
endloop  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Versions (God Client)  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x3E** 
Sent By: Server  
Size: 37  

  
**Packet Build**  
BYTE\[1\] Cmd  
BYTE\[4\] Unknown  
BYTE\[4\] Unknown  
BYTE\[4\] Unknown  
BYTE\[4\] Unknown  
BYTE\[4\] Unknown  
BYTE\[4\] Unknown  
BYTE\[4\] Unknown  
BYTE\[4\] Unknown  
BYTE\[4\] Unknown  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Update Statics (God Client)  
Last Modified: 2009-03-02 05:16:01  
Modified By: Turley  
  
**Packet: 0x3F** 
Sent By: Server  
Size: variable  

  
**Packet Build**  
BYTE\[1\] Cmd  
BYTE\[2\] Length  
BYTE\[4\] The block number of the statics grid. This can be derived by the following formula: Block\_Num = X / 8 \* MapHeight + Y / 8 .... MapHeight is equal to the total number of tiles in the Y-axis divided by eight (0x0200 for map0.mul).  
BYTE\[4\] # of static items in grid.  
BYTE\[4\] "Extra" Value in statics index file.  
Item Loop:  
* BYTE\[2\] Item ID
* BYTE\[1\] Item's X pos relative to top left corner of grid.
* BYTE\[1\] Item's Y pos relative to top left corner of grid.
* BYTE\[1\] Item's Z pos.
* BYTE\[2\] Item's Hue.

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Personal Light Level  
Last Modified: 2009-03-02 05:16:12  
Modified By: Turley  
  
**Packet: 0x4E** 
Sent By: Server  
Size: 6 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] creature id  
BYTE\[1\] level  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Overall Light Level  
Last Modified: 2009-03-02 04:38:40  
Modified By: Turley  
  
**Packet: 0x4F** 
Sent By: Server  
Size: 2 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] level  
* 0x00 - day
* 0x09 - OSI night
* 0x1F - Black
* Max normal val = 0x1F

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Reject Character Logon  
Last Modified: 2009-12-18 09:21:35  
Modified By: Tomi  
  
**Packet: 0x53** 
Sent By: Server  
Size: 2 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] value  

  
**Subcommand Build**  
N/A

  
**Notes**  
value;  
  
0x00 - Incorrect Password  
0x01 - This character does not exist any more!  
0x02 - This character already exists.  
0x03 - Could not attach to game server.  
0x04 - Could not attach to game server.  
0x05 - Another character is logged in.  
0x06 - Synchronization Error.  
0x07 - Idle too long.  
0x08 - Could not attach to game server.  
0x09 - Character Transfer.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Play Sound Effect  
Last Modified: 2009-03-02 05:16:35  
Modified By: Turley  
  
**Packet: 0x54** 
Sent By: Server  
Size: 12 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] mode (0x00=quiet, repeating, 0x01=single normally played sound effect)  
BYTE\[2\] SoundModel  
BYTE\[2\] unknown3 (speed/volume modifier? Line of sight stuff?)  
BYTE\[2\] xLoc  
BYTE\[2\] yLoc  
BYTE\[2\] zLoc  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Login Complete  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x55** 
Sent By: Server  
Size: 1 byte  

  
**Packet Build**  
BYTE\[1\] cmd  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Time  
Last Modified: 2009-03-02 05:16:47  
Modified By: Turley  
  
**Packet: 0x5B** 
Sent By: Server  
Size: 4 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] hour  
BYTE\[1\] minute  
BYTE\[1\] second  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Set Weather  
Last Modified: 2009-05-12 10:17:44  
Modified By: Tomi  
  
**Packet: 0x65** 
Sent By: Server  
Size: 4 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] type (See Notes)  
BYTE\[1\] num (number of weather effects on screen)  
BYTE\[1\] temperature  

  
**Subcommand Build**  
N/A

  
**Notes**  
Types: 0x00 - "It starts to rain", 0x01 - "A fierce storm approaches.", 0x02 - "It begins to snow", 0x03 - "A storm is brewing.", 0xFF - None (turns off sound effects), 0xFE (no effect?? Set temperature?)   
  
  
Maximum number of weather effects on screen is 70.  
  
If it is raining, you can add snow by setting the num to the num of rain currently going, plus the number of snow you want.   
  
Weather messages are only displayed when weather starts.  
  
  
With Old clients the weather will end automatically after 6 minutes.  
With New clients ( unsure when changed ) the weather will end automatically after 10 minutes.  
  
To avoid the weather to end automatically you have to resend the weather packet during that time.  
  
If you resend weather packet 1 time it will reset the 6 or 10 minute timer and start it over again.  
  
If you resend the weather packet more than once it will reset the time and start a timer with doubled time no matter how many times you resend, it always just doubled the timer in my tests.  
  
  
You can totally end weather (to display a new message) by teleporting. I think it's either the 0x78 or 0x20 messages that reset it, havent checked it though.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Play Midi Music  
Last Modified: 2009-03-02 05:17:30  
Modified By: Turley  
  
**Packet: 0x6D** 
Sent By: Server  
Size: 3 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] musicID  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Character Animation  
Last Modified: 2009-07-28 05:24:26  
Modified By: MuadDib  
  
**Packet: 0x6E** 
Sent By: Server  
Size: 14 Bytes  

  
**Packet Build**  
BYTE\[1\] Cmd  
BYTE\[4\] Object Serial  
BYTE\[2\] Action  
BYTE\[1\] unknown1 (0x00)  
BYTE\[1\] Frame Count  
BYTE\[2\] Repeat (1 = once / 2 = twice / 0 = repeat forever)   
BYTE\[1\] Forward/Backwards(0x00=forward, 0x01=backwards)   
BYTE\[1\] Repeat Flag (0 - Don't repeat / 1 repeat)   
BYTE\[1\] Frame Delay (0x00 - fastest / 0xFF - Too slow to watch)  

  
**Subcommand Build**  
N/A

  
**Notes**  
Action Flags:  
* 0x00 = walk
* 0x01 = walk faster
* 0x02 = run
* 0x03 = run (faster?)
* 0x04 = nothing
* 0x05 = shift shoulders
* 0x06 = hands on hips
* 0x07 = attack stance (short)
* 0x08 = attack stance (longer)
* 0x09 = swing (attack with knife)
* 0x0a = stab (underhanded)
* 0x0b = swing (attack overhand with sword)
* 0x0c = swing (attack with sword over and side)
* 0x0d = swing (attack with sword side)
* 0x0e = stab with point of sword
* 0x0f = ready stance
* 0x10 = magic (butter churn!)
* 0x11 = hands over head (balerina)
* 0x12 = bow shot
* 0x13 = crossbow
* 0x14 = get hit
* 0x15 = fall down and die (backwards)
* 0x16 = fall down and die (forwards)
* 0x17 = ride horse (long)
* 0x18 = ride horse (medium)
* 0x19 = ride horse (short)
* 0x1a = swing sword from horse
* 0x1b = normal bow shot on horse
* 0x1c = crossbow shot
* 0x1d = block #2 on horse with shield
* 0x1e = block on ground with shield
* 0x1f = swing, and get hit in middle
* 0x20 = bow (deep)
* 0x21 = salute
* 0x22 = scratch head
* 0x23 = 1 foot forward for 2 secs
* 0x24 = same

  
---

  
[\[^\]](#TOP)

**Packet Name:** Graphical Effect  
Last Modified: 2009-03-02 04:44:00  
Modified By: Turley  
  
**Packet: 0x70** 
Sent By: Server  
Size: 28 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[1\] direction type   
* 00 = go from source to dest
* 01 = lightning strike at source
* 02 = stay at current x,y,z
* 03 = stay with current source character id
BYTE\[4\] character id   
BYTE\[4\] target id  
BYTE\[2\] model of the first frame of the effect  
BYTE\[2\] xLoc   
BYTE\[2\] yLoc   
BYTE zLoc   
BYTE\[2\] xLoc of target   
BYTE\[2\] yLoc of target   
BYTE\[1\] zLoc of target   
BYTE\[1\] speed of the animation  
BYTE\[1\] duration (0=really long, 1= shortest)  
BYTE\[2\] unknown2 (0 works)  
BYTE\[1\] adjust direction during animation (1=no)  
BYTE\[1\] explode on impact  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Open Buy Window  
Last Modified: 2009-03-02 04:44:50  
Modified By: Turley  
  
**Packet: 0x74** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[2\] blockSize   
BYTE\[4\] (vendorID | 0x40000000)   
BYTE\[1\] # of items   
\# of items worth of item segments   
* BYTE\[4\] price
* BYTE length of text description
* BYTE\[text length\] item description

  
**Subcommand Build**  
N/A

  
**Notes**  
This packet is always preceded by a describe contents packet (0x3c) with the container id as the (vendorID | 0x40000000) and then an open container packet (0x24?) with the vendorID only and a model number of 0x0030 (probably the model # for the buy screen)  

  
---

  
[\[^\]](#TOP)

**Packet Name:** New Subserver  
Last Modified: 2009-03-02 05:17:51  
Modified By: Turley  
  
**Packet: 0x76** 
Sent By: Server  
Size: 16 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] xLoc  
BYTE\[2\] yLoc  
BYTE\[2\] zLoc  
BYTE\[1\] unknown (always 0)  
BYTE\[2\] server boundry X  
BYTE\[2\] server boundry Y  
BYTE\[2\] server boundry Width  
BYTE\[2\] server boundry Height  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Update Player  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x77** 
Sent By: Server  
Size: 17 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd   
BYTE\[4\] player id   
BYTE\[2\] model  
BYTE\[2\] xLoc   
BYTE\[2\] yLoc   
BYTE\[1\] zLoc   
BYTE\[1\] direction   
BYTE\[2\] hue/skin color   
BYTE\[1\] status flag (bit field)   
BYTE\[1\] highlight color  

  
**Subcommand Build**  
N/A

  
**Notes**  
01-10-2007  
Interesting, 0x12 Status Flag was a guild mates mount not in war mode.  
0x52 is for the same pets status flag while in war mode. Poisoned it stayed the same appropriately in each. 0x12 and 0x52  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Draw Object  
Last Modified: 2009-03-01 12:30:16  
Modified By: MuadDib  
  
**Packet: 0x78** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[2\] Length  
BYTE\[4\] Object Serial  
BYTE\[2\] Graphic ID  
BYTE\[2\] X  
BYTE\[2\] Y  
BYTE\[1\] Z  
BYTE\[1\] Direction/Facing  
BYTE\[2\] Color  
BYTE\[1\] Status Flag (bitwise flag)  
BYTE\[1\] Notoriety  
**If (BYTE\[4\] == 0x00 0x00 0x00 0x00)**  
BYTE\[1\] 0x0  
**Otherwise loop until above BYTE\[4\] is satisfied.**  
* BYTE\[4\] Serial
* BYTE\[2\] Graphic
* BYTE\[1\] Layer
* BYTE\[2\] Color (this byte only needed if (Graphic&0x8000)
BYTE\[4\] 0x00000000  

  
**Subcommand Build**  
N/A

  
**Notes**  
**Status Options**  
0x00: Normal  
0x01: Unknown  
0x02: Can Alter Paperdoll  
0x04: Poisoned  
0x08: Golden Health  
0x10: Unknown  
0x20: Unknown  
0x40: War Mode  
  
**Notoriety**  
0x1: Innocent (Blue)  
0x2: Friend (Green)  
0x3: Grey (Grey - Animal)  
0x4: Criminal (Grey)  
0x5: Enemy (Orange)  
0x6: Murderer (Red)  
0x7: Invulnerable (Yellow)  
  
01-10-2007  
Interesting, 0x12 Status Flag was a guild mates mount not in war mode.   
0x52 is for the same pets status flag while in war mode. Poisoned it stayed the same appropriately in each. 0x12 and 0x52  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Open Dialog Box  
Last Modified: 2009-03-02 04:47:44  
Modified By: Turley  
  
**Packet: 0x7C** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] blockSize  
BYTE\[4\] dialogID (echo'd back to the server in 7d)  
BYTE\[2\] menuid (echo'd back to server in 7d)  
BYTE\[1\] length of question  
BYTE\[length of question\] question text  
BYTE\[1\] # of responses  
**Then for each response:**  
* BYTE\[2\] model id # of shown item (if grey menu -- then always 0x00 as msb)
* BYTE\[2\] color of shown item
* BYTE response text length
* BYTE\[response text length\] response text

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Login Denied  
Last Modified: 2009-03-02 05:18:11  
Modified By: Turley  
  
**Packet: 0x82** 
Sent By: Server  
Size: 2 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] reason  

  
**Subcommand Build**  
N/A

  
**Notes**  
Login denied reasons (gump message):  
  
0x00 - Incorrect name/password.  
0x01 - Someone is already using this account.  
0x02 - Your account has been blocked.  
0x03 - Your account credentials are invalid.  
0x04 - Communication problem.  
0x05 - The IGR concurrency limit has been met.  
0x06 - The IGR time limit has been met.  
0x07 - General IGR authentication failure.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Resend Characters After Delete  
Last Modified: 2009-03-02 04:48:32  
Modified By: Turley  
  
**Packet: 0x86** 
Sent By: Server  
Size: 304 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] blockSize  
BYTE\[1\] # of characters  
**Following repeated 5 times**  
* BYTE\[30\] character name
* BYTE\[30\] character password

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Open Paperdoll  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x88** 
Sent By: Server  
Size: 66 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] charid  
BYTE\[60\] text  
BYTE\[1\] flag byte  

  
**Subcommand Build**  
N/A

  
**Notes**  
Flag Notes:  
Normal = 0x00,  
Unknown = 0x01,  
Can Alter Paperdoll = 0x02,  
Poisoned = 0x04,  
Golden Health = 0x08,  
Unknown2 = 0x10,  
Unknown3 = 0x20,  
War Mode = 0x40,  
Hidden = 0x80  
  
Can Alter Paperdoll only exists in 3.x+ clients? Verified up to 2.0.4 does not use this.  
As Of AOS Clients (above 3.0.8z, and initial, the buggy ones), these have changed. Warmode is now 0x01.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Corpse Clothing  
Last Modified: 2009-03-02 05:18:38  
Modified By: Turley  
  
**Packet: 0x89** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] blockSize  
BYTE\[4\] corpseID  
BYTE\[1\] itemLayer  
BYTE\[4\] itemID  
BYTE\[1\] terminator (0x00)  

  
**Subcommand Build**  
N/A

  
**Notes**  
Followed by a 0x3C message with the contents.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Connect To Game Server  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0x8C** 
Sent By: Server  
Size: 11 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] gameServer IP   
BYTE\[2\] gameServer port   
BYTE\[4\] new key  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Map Message  
Last Modified: 2009-08-13 09:42:49  
Modified By: MuadDib  
  
**Packet: 0x90** 
Sent By: Server  
Size: 19 Bytes  

  
**Packet Build**  
BYTE\[1\] 0x90  
BYTE\[4\] Map Serial  
BYTE\[2\] Gump Art (usually is 0x139D) For Corner Image  
BYTE\[2\] Upper Left X  
BYTE\[2\] Upper Left Y  
BYTE\[2\] Lower Right X  
BYTE\[2\] Lower Right Y  
BYTE\[2\] Gump Width  
BYTE\[2\] Gump Height  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Request Assistance  
Last Modified: 2009-07-28 07:44:28  
Modified By: MuadDib  
  
**Packet: 0x9C** 
Sent By: Server  
Size: 53  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[52\] 0x0 (Unknown details at this time)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Sell List  
Last Modified: 2009-03-02 04:50:17  
Modified By: Turley  
  
**Packet: 0x9E** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] blockSize  
BYTE\[4\] shopkeeperID  
BYTE\[2\] numItems  
**For each item, a structure containing:**  
* BYTE\[4\] itemID
* BYTE\[2\] itemModel
* BYTE\[2\] itemHue/Color
* BYTE\[2\] itemAmount
* BYTE\[2\] value
* BYTE\[2\] nameLength
* BYTE\[?\] name

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Update Current Health  
Last Modified: 2009-03-02 05:19:08  
Modified By: Turley  
  
**Packet: 0xA1** 
Sent By: Server  
Size: 9 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] playerID  
BYTE\[2\] maxHealth  
BYTE\[2\] currentHealth  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Update Current Mana  
Last Modified: 2009-03-02 05:19:17  
Modified By: Turley  
  
**Packet: 0xA2** 
Sent By: Server  
Size: 9 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] playerID  
BYTE\[2\] maxMana  
BYTE\[2\] currentMana  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Update Current Stamina  
Last Modified: 2009-03-02 05:19:32  
Modified By: Turley  
  
**Packet: 0xA3** 
Sent By: Server  
Size: 9 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] playerID  
BYTE\[2\] maxStamina  
BYTE\[2\] currentStamina  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Open Web Browser  
Last Modified: 2009-03-02 05:19:41  
Modified By: Turley  
  
**Packet: 0xA5** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] blockSize  
BYTE\[blockSize-3\] null terminated full web address  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Tip/Notice Window  
Last Modified: 2009-03-02 04:51:22  
Modified By: Turley  
  
**Packet: 0xA6** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] blockSize  
BYTE\[1\] flag  
* 0x00 - tips window
* 0x01 - ignored
* 0x02 - updates
BYTE\[4\] tip #  
BYTE\[2\] msgSize  
BYTE\[msgSize\] message (ascii, not null terminated)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Game Server List  
Last Modified: 2009-03-02 04:52:25  
Modified By: Turley  
  
**Packet: 0xA8** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
BYTE\[1\] System Info Flag  
BYTE\[2\] # of servers  
  
**Then each server --**  
* BYTE\[2\] server index (0-based)
* BYTE\[32\] server name
* BYTE percent full
* BYTE timezone
* BYTE\[4\] server ip to ping

  
**Subcommand Build**  
N/A

  
**Notes**  
System Info Flags: 0xCC - Do not send video card info. 0x64 - Send Video card. RunUO And SteamEngine both send 0x5D.  
  
Server IP has to be sent in reverse order. For example, 192.168.0.1 is sent as 0100A8C0.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Characters / Starting Locations  
Last Modified: 2010-01-07 09:02:29  
Modified By: Tomi  
  
**Packet: 0xA9** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] packet length  
BYTE\[1\] number of characters (slots 5, 6 or 7)  
loop characters (5, 6 or 7):  
BYTE\[30\] character name  
BYTE\[30\] character password  
endloop(characters)  
BYTE\[1\] number of starting locations (cities)  
loop # of cities:  
BYTE\[1\] locationIndex (0-based)  
BYTE\[31\] city name(general name)  
BYTE\[31\] area of city or town  
endloop(# of cities)  
BYTE\[4\] Flags (see notes)  

  
**Subcommand Build**  
N/A

  
**Notes**  
Flags list:  
0x01: unknown  
0x02: send config/req logout (IGR?, overwrite configuration button?)  
0x04: single character (siege - limit 1 character/account)  
0x08: enable npcpopup/context menus  
0x10: limit character slots?  
0x20: enable common AOS features (tooltip thing/fight system book, but not AOS monsters/map/skills, necromancer/paladin classes)  
0x40: 6th character slot  
0x80: samurai and ninja classes  
0x100: elven race  
0x200: KR support flag1 ?  
0x400: send UO3D client type (client will send 0xE1 packet)  
0x800: unknown  
0x1000: 7th character slot, only 2D client  
0x2000: unknown (SA?)  
  
Flags can be combined as literals to view. Such as to have NPC Popup Menus AND Common AOS Features, send as 0x28, not 0x8 plus 0x20 form.  
Another example: elven race, samurai classes and AOS will be 0x1A0.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Allow/Refuse Attack  
Last Modified: 2009-03-02 05:20:01  
Modified By: Turley  
  
**Packet: 0xAA** 
Sent By: Server  
Size: 5 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] CharID being attacked  

  
**Subcommand Build**  
N/A

  
**Notes**  
ID is set to 00 00 00 00 when attack is refused.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Gump Text Entry Dialog  
Last Modified: 2009-03-02 05:20:25  
Modified By: Turley  
  
**Packet: 0xAB** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] blockSize  
BYTE\[4\] id  
BYTE\[1\] parentID  
BYTE\[1\] buttonID  
BYTE\[1\] textlen  
BYTE\[?\] text  
BYTE\[1\] cancel (0=disable, 1=enable)  
BYTE\[1\] style (0=disable, 1=normal, 2=numerical)  
BYTE\[4\] format (if style 1, max text len, if style2, max numeric value)  
BYTE\[1\] text2 length  
BYTE\[?\] text2  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Unicode Speech message  
Last Modified: 2009-03-02 04:53:10  
Modified By: Turley  
  
**Packet: 0xAE** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
BYTE\[4\] ID  
BYTE\[2\] Model  
BYTE\[1\] Type  
BYTE\[2\] Color  
BYTE\[2\] Font  
BYTE\[4\] Language  
BYTE\[30\] Name  
BYTE\[?\]\[2\] Msg - Null Terminated (blockSize - 48)  

  
**Subcommand Build**  
N/A

  
**Notes**  
The various types of text is as follows:  
0x00 - Normal   
0x01 - Broadcast/System  
0x02 - Emote   
0x06 - System/Lower Corner  
0x07 - Message/Corner With Name  
0x08 - Whisper  
0x09 - Yell  
0x0A - Spell  
0x0D - Guild Chat  
0x0E - Alliance Chat  
0x0F - Command Prompts  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Display Death Action  
Last Modified: 2009-03-02 05:20:36  
Modified By: Turley  
  
**Packet: 0xAF** 
Sent By: Server  
Size: 13 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] player id  
BYTE\[4\] corpse id  
BYTE\[4\] unknown (all 0)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Send Gump Menu Dialog  
Last Modified: 2009-03-02 04:53:51  
Modified By: Turley  
  
**Packet: 0xB0** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] blockSize  
BYTE\[4\] id  
BYTE\[4\] gumpid  
BYTE\[4\] x  
BYTE\[4\] y  
BYTE\[2\] command section length  
BYTE\[?\] commands (zero terminated)  
BYTE\[2\] numTextLines  
* BYTE\[2\] text length (in unicode (2 byte) characters.)
* BYTE\[?\] text (in unicode)

  
**Subcommand Build**  
N/A

  
**Notes**  
In gump packets, and with any EMU (POL for sure) the "ID" is the # of the script's PID that fired the gump. This can be used via specific 0xBF packets to send the ID to the client to force closing of the gump from the server. When this is done, the client Auto Responds as if it had "exited" the menu normally so the serverside script will handle it as such.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Chat Message  
Last Modified: 2009-03-02 04:56:24  
Modified By: Turley  
  
**Packet: 0xB2** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
BYTE\[2\] messageType  
Messagetypes will be listed as Subcommands.  

  
**Subcommand Build**  
**Messagetypes 0x0001 - 0x0024:**  
BYTE\[4\] unknown (00 00 00 00)  
BYTE\[len-9\] Unicode for %1 if the message type has a %1 variable for inputting text.  
BYTE\[2\] Null Terminator if there is a %2 Unicode element. (00 00)  
BYTE\[len-(2+%1+9)\] Unicode for %2 if the message type has a %2 variable for inputting text.  
BYTE\[2\] Null Terminator for Packet (00 00)  
  
**Messagetypes 0x0028 - 0x002C:**  
BYTE\[4\] unknown (00 00 00 00)  
BYTE\[len-9\] Unicode for %1 if the message type has a %1 variable for inputting text.  
BYTE\[2\] Null Terminator if there is a %2 Unicode element. (00 00)  
BYTE\[len-(2+%1+9)\] Unicode for %2 if the message type has a %2 variable for inputting text.  
BYTE\[2\] Null Terminator for Packet (00 00)  
  
**MessageType 0x0025 (Message), 0x0026 (Emote), 0x0027 (OOC):**  
BYTE\[3\] Language Code (ENU for english)  
BYTE\[1\] Null Terminator (00)  
BYTE\[2\] Message from (see notes)  
BYTE\[?\] Username in Unicode  
BYTE\[2\] Null Terminator for Username (00 00)  
BYTE\[?\] Unicode Message Sent  
BYTE\[2\] Null Terminator for Message  
  
**MessageType 0x03E8 (create conference):**  
BYTE\[4\] Unknown (00 00 00 00)  
BYTE\[?\] Unicode Channel Name  
BYTE\[2\] Null Terminator for Channel Name (00 00)  
BYTE\[2\] Password Setting (00 30 = No Pass, 00 31 = Password required)  
BYTE\[2\] Null Terminator for packet (00 00)  
  
**MessageType 0x03E9 (Destroy conference):**  
BYTE\[4\] Unknown (00 00 00 00)  
BYTE\[?\] Unicode Channel Name  
BYTE\[4\] Unknown (00 00 00 00)  
  
**MessageType 0x03EB (Display Enter Username window):**  
BYTE\[8\] Unknown (00 00 00 00 00 00 00 00)  
  
**MessageType 0x03EC (Close Chat):**  
BYTE\[8\] Unknown (00 00 00 00 00 00 00 00)  
  
**MessageType 0x03ED (Username accepted, display chat):**  
BYTE\[4\] Unknown (00 00 00 00)  
BYTE\[?\] Unicode Username  
BYTE\[4\] Null Terminator (00 00 00 00)  
  
**MessageType 0x03EE (Add User):**  
BYTE\[4\] Unknown (00 00 00 00)  
BYTE\[2\] User Type (see notes)  
BYTE\[?\] Unicode Username  
BYTE\[2\] Null Terminator (00 00)  
  
**MessageType 0x03EF (Remove User):**  
BYTE\[4\] Unknown (00 00 00 00)  
BYTE\[?\] Unicode Username  
BYTE\[2\] Null Terminator (00 00)  
  
**MessageType 0x03F0 (Clear All Players):**  
BYTE\[8\] Unknown (00 00 00 00 00 00 00 00)  
  
**MessageType 0x03F1 (You have joined the %1 Conference):**  
BYTE\[4\] Unknown (00 00 00 00)  
BYTE\[?\] Unicode Conference Name (%1)  
BYTE\[4\] Unknown (00 00 00 00)  

  
**Notes**  
**MessageTypes 0x0001 - 0x0024 and 0x0028 - 0x002C: Directly from Chat.enu file:**  
0x0001: You are already ignoring the maximum number of peolpe.  
0x0002: You are already ignoring %1.  
0x0003: You are now ignoring %1.  
0x0004: You are no longer ignoring %1.  
0x0005: You are not ignoring %1.  
0x0006: You are no longer ignoring anyone.  
0x0007: That is not a valid conference name.  
0x0008: There is already a conference of that name.  
0x0009: You must have operator status to do this.  
0x000A: Conference %1 renamed to %2.  
0x000B: You must be in a conference to do this. To join a conference, select one from the Conference menu.  
0x000C: There is no player named '%1'.  
0x000D: There is no conference named '%1'.  
0x000E: That is not the correct password.  
0x000F: %1 has chosen to ignore you. None of your messages to them will get through.  
0x0010: The moderator of this conference has not given you speaking privileges.  
0x0011: You can now receive private messages.  
0x0012: You will no longer receive private messages. Those who send you a message will be notified that you are blocking incoming messages.  
0x0013: You are now showing your character name to any players who inquire with the whois command.  
0x0014: You are no longer showing your character name to any players who inquire with the whois command.  
0x0015: %1 is remaining anonymous.  
0x0016: %1 has chosen to not receive private messages at the moment.  
0x0017: %1 is known in the lands of Britannia as %2.  
0x0018: %1 has been kicked out of the conference.  
0x0019: %1, a conference moderator, has kicked you out of the conference.  
0x001A: You are already in the conference '%1'.  
0x001B: %1 is no longer a conference moderator.  
0x001C: %1 is now a conference moderator.  
0x001D: %1 has removed you from the list of conference moderators.  
0x001E: %1 has made you a conference moderator.  
0x001F: %1 no longer has speaking privileges in this conference.  
0x0020: %1 now has speaking privileges in this conference.  
0x0021: %1, a conference moderator, has removed your speaking privileges for this conference.  
0x0022: %1, a conference moderator, has granted you speaking privileges in this conference.  
0x0023: From now on, everyone in the conference will have speaking privileges by default.  
0x0024: From now on, only moderators will have speaking privileges in this conference by default.  
0x0028: The password to the conference has been changed.  
0x0029: Sorry--the conference named '%1' is full and no more players are allowed in.  
0x002A: You are banning %1 from this conference.  
0x002B: %1, a conference moderator, has banned you from the conference.  
0x002C: You have been banned from this conference.  
  
**MessageTypes 0025, 0026, and 0027: Message from is as follows:**  
0x0030 = user, 0x0031 = moderator, 0x0032 = muted, 0x0034 = me, 0x0035 = system  
  
**MessageType 03EE:**   
User types are 0030 for User, 0031 for Moderator, and 0032 for Muted User.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Help/Tip Data  
Last Modified: 2009-03-02 05:20:54  
Modified By: Turley  
  
**Packet: 0xB7** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
BYTE\[4\] id  
BYTE\[?\]\[2\] message, in unicode  
BYTE\[2\] null terminator (0x0000)  
BYTE\[2\] message terminator (0x3300)  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Enable locked client features  
Last Modified: 2010-01-07 09:05:23  
Modified By: Tomi  
  
**Packet: 0xB9** 
Sent By: Server  
Size: 3/5 Bytes  

  
**Packet Build**  
Original Legacy Client Version  
BYTE\[1\] 0xB9  
BYTE\[2\] Feature Bitflag  
  
From Legacy Client 6.0.14.2+  
BYTE\[1\] 0xB9  
BYTE\[4\] Feature Bitflag  

  
**Subcommand Build**  
N/A

  
**Notes**  
feature flags:  
  
0x01: enable T2A features: chat, regions  
0x02: enable renaissance features  
0x04: enable third dawn features  
0x08: enable LBR features: skills, map  
0x10: enable AOS features: skills, map, spells, fightbook  
0x20: 6th character slot  
0x40: enable SE features  
0x80: enable ML features: elven race, spells, skills  
0x100: enable 8th age splash screen  
0x200: enable 9th age splash screen  
0x400: enable 10th age  
0x800: enable increased housing and bank storage  
0x1000: 7th character slot  
0x2000: enable KR faces  
0x4000: enable trial account  
0x8000: enable 11th age  
0x10000: enable SA features: gargoyle race, spells, skills  
  
This packet is send immediatly after login.  
If you need several features you have to summ the flags.  
  
  
Older notes (not sure which are correct!):  
  
if (MSB not set)   
Bit# 1 T2A upgrade, enables chatbutton   
Bit# 2 enables LBR update. (of course LBR installation is required)(plays MP3 instead of midis, 2D LBR client shows new LBR monsters,...)   
if (MSB set)   
Bit# 3 T2A upgrade, enables chatbutton   
Bit# 4 enables LBR update   
Bit#5 enables AOS update (Aos monsters/map (AOS installation required for that) , AOS skills/necro/paladin/fight book stuff - works for ALL clients 4.0 and above)   
  
  
Examples:   
ML: B980FB   
AOS-7AV: B9803B   
AOS: B9801B   
LBR: B90003  
  
  
Custom housing uses these flags to enable some parts.  
  
for FeatureMask check floors.txt, roofs.txt etc in client folder  
  
FeatureMask 0 = AOS ( flag 0x10 )  
FeatureMask 64 = SE ( flag 0x40 )  
FeatureMask 128 = ML ( flag 0x80 )  
FeatureMask 512 = 9th age ( flag 0x200 )  
FeatureMash 65536 = SA ( flag 0x10000 )  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Quest Arrow  
Last Modified: 2009-03-02 05:21:09  
Modified By: Turley  
  
**Packet: 0xBA** 
Sent By: Server  
Size: 6 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] active (1=on, 0=off)  
BYTE\[2\] xLoc  
BYTE\[2\] yLoc  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Seasonal Information  
Last Modified: 2009-07-28 14:14:43  
Modified By: MuadDib  
  
**Packet: 0xBC** 
Sent By: Server  
Size: 3 Bytes  

  
**Packet Build**  
BYTE\[1\] 0xBC  
BYTE\[1\] Season Flag  
BYTE\[1\] Play Sound (0/1)  

  
**Subcommand Build**  
N/A

  
**Notes**  
Season Flag:  
0: Spring  
1: Summer  
2: Fall  
3: Winter  
4: Desolation  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Graphical Effect  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0xC0** 
Sent By: Server  
Size: 36 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[1\] type  
BYTE\[4\] sourceSerial   
BYTE\[4\] targetSerial   
BYTE\[2\] itemID   
BYTE\[2\] xSource   
BYTE\[2\] ySource   
BYTE\[1\] zSource   
BYTE\[2\] xTarget   
BYTE\[2\] yTarget   
BYTE\[1\] zTarget   
BYTE\[1\] speed   
BYTE\[1\] duration   
BYTE\[2\] unk // On OSI, flamestrikes are 0x0100   
BYTE\[1\] fixedDirection   
BYTE\[1\] explodes   
BYTE\[4\] hue   
BYTE\[4\] renderMode  

  
**Subcommand Build**  
N/A

  
**Notes**  
Packet seems to work fine with even 2.0.0 clients  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Cliloc Message  
Last Modified: 2009-03-02 04:58:53  
Modified By: Turley  
  
**Packet: 0xC1** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
BYTE\[4\] id (0xffff for system message)  
BYTE\[2\] body (0xff for system message)  
BYTE\[1\] type (6 - lower left, 7 on player)  
BYTE\[2\] hue  
BYTE\[2\] font  
BYTE\[4\] Message number  
BYTE\[30\] - speaker's name  
BYTE\[?\*2\] - arguments // \_little-endian\_ unicode string, tabs ('\\t') seperate the arguments. Null Terminated 0x0000  

  
**Subcommand Build**  
N/A

  
**Notes**  
Argument example:  
take number 1042762:  
"Only \~1\_AMOUNT\~ gold could be deposited. A check for \~2\_CHECK\_AMOUNT\~ gold was returned to you."  
the arguments string may have "100 thousand\\t25 hundred", which in turn would modify the string:  
"Only 100 thousand gold could be deposited. A check for 25 hundred gold was returned to you."  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Semivisible (Smurf it!)  
Last Modified: 2009-03-02 05:21:31  
Modified By: Turley  
  
**Packet: 0xC4** 
Sent By: Server  
Size: 6 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[4\] ID  
BYTE\[1\] intensity ?  

  
**Subcommand Build**  
N/A

  
**Notes**  
It sets target more or less blue (dependant on intensity value). Only for 2d clients, for 3d clients it does nothing in last attempts (pre-4.x client test)  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Invalid Map Enable  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0xC6** 
Sent By: Server  
Size: 0x1  

  
**Packet Build**  
BYTE\[1\] CMD  

  
**Subcommand Build**  
N/A

  
**Notes**  
Sent to client to let it know it tried to enable/use an invalid map. Exact purpose unknown to myself.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** 3D Particle Effect  
Last Modified: 2009-03-02 05:00:21  
Modified By: Turley  
  
**Packet: 0xC7** 
Sent By: Server  
Size: 49 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[35\] ...Preamble .. exactly like 0xC0  
BYTE\[2\] effect # (tile ID)  
BYTE\[2\] explode effect # (0 if no explosion)  
BYTE\[2\] additional effect # that's only used for moving effects, 0 otherwise  
BYTE\[4\] if target is item (type 2) that's itemId, 0 otherwise  
BYTE\[1\] layer (of the character, e.g left hand, right hand, ... 0-4, 0xff: moving effect or target is no char)  
BYTE\[2\] yet another (unknown) additional effect that's only set for moving effect, 0 otherwise  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Global Que Count  
Last Modified: 2009-03-25 12:52:46  
Modified By: MuadDib  
  
**Packet: 0xCB** 
Sent By: Server  
Size: 7 Bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[2\] Unknown (0x0)  
BYTE\[4\] Count (Help requests in the que)  

  
**Subcommand Build**  
N/A

  
**Notes**  
Displays the message: There are currently \[count\] available calls in the global queue  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Cliloc Message Affix  
Last Modified: 2009-03-02 05:01:08  
Modified By: Turley  
  
**Packet: 0xCC** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
BYTE\[4\] id (0xffff for system message)  
BYTE\[2\] body (0xff for system message)  
BYTE\[1\] type (6 - lower left, 7 on player)  
BYTE\[2\] hue  
BYTE\[2\] font  
BYTE\[4\] Message number  
BYTE\[1\] flags (0x02 is unknown, 0x04 signals the message doesn't move on screen) (flags & 0x1) == 0 signals affix is appended to the localization string, (flags & 0x1) == 1 signals to prepend.  
BYTE\[30\] - speaker's name  
BYTE\[?\] affix / null terminated  
BYTE\[?\]\*2\] arguments; // \_big-endian\_ unicode string, tabs ('\\t') seperate arguments, see 0xC1 for argument example  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Extended 0x20  
Last Modified: 2009-03-02 05:21:59  
Modified By: Turley  
  
**Packet: 0xD2** 
Sent By: Server  
Size: 25 Bytes  

  
**Packet Build**  
BYTE\[1\] cmd  
Preamble: Exactly like 0x20  
BYTE\[2\] unknown 1  
BYTE\[2\] unknown 2  
BYTE\[2\] unknown 3  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Extended 0x78  
Last Modified: 2009-03-02 05:22:13  
Modified By: Turley  
  
**Packet: 0xD3** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
Preamble: Exactly like 0x78  
BYTE\[2\] unknown 1  
BYTE\[2\] unknown 2  
BYTE\[2\] unknown 3  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Send Custom House  
Last Modified: 2009-03-02 05:02:47  
Modified By: Turley  
  
**Packet: 0xD8** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length  
BYTE\[1\] CompressionType (0=nothing, 3=zlib)  
BYTE\[1\] unknown (0)  
BYTE\[4\] HouseSerial  
BYTE\[4\] Revision State  
BYTE\[2\] Number of Tiles  
BYTE\[2\] Buffer length (Number of tiles \* 5)  
**For Each Component**  
* BYTE\[2\] component graphic
* BYTE\[1\] X
* BYTE\[1\] Y
* BYTE\[1\] Z
  
X/Y of this packet is probably wrong. May be 2 Byte portions instead of 1\. Information was submitted and as of yet untested.  

  
**Subcommand Build**  
N/A

  
**Notes**  
N/A

  
---

  
[\[^\]](#TOP)

**Packet Name:** Character Transfer Log  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0xDB** 
Sent By: Server  
Size: variable  

  
**Packet Build**  
BYTE\[1\] cmd  
BYTE\[2\] length of packet  
BYTE\[4\] Unknown, always 000000BB in my testing.  
BYTE\[?\] Length - 7 of packet  
BYTE\[4\] Transfer ID  
BYTE\[4\] Unknown, all zeros  
BYTE\[4\] Unknown, all zeros  
For each item in transfer log, this loop is performed:  
 BYTE\[4\] Item's Serial  
 BYTE\[4\] Size of next loop (Big Endian)  
 BYTE\[4\] The cliloc of the item's name or property. If zero, defaults to 'Transfer Crate.'  
 BYTE\[1\] Length of Cliloc Addon Text  
 BYTE\[?\] Text to affix/insert into the cliloc  
 BYTE\[2\] 0009 - Ends the loop for cliloc addon text and item  

  
**Subcommand Build**  
N/A

  
**Notes**  
When a character transfer is confirmed, the server sends this to create a log of all the items being included in the transfer.  
This log is saved to translog.txt.  
Item properties are also included in this packet.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** SE Introduced Revision  
Last Modified: 2008-10-11 09:57:22  
Modified By: MuadDib  
  
**Packet: 0xDC** 
Sent By: Server  
Size: 9  

  
**Packet Build**  
BYTE\[1\] CMD  
BYTE\[4\] Item/Mob Serial  
BYTE\[4\] Item Revision Hash  

  
**Subcommand Build**  
N/A

  
**Notes**  
Newer packet as of late 2004\. Is now used on OSI to handle the Revision of Tooltips instead of the original 0xBF methods it appears.  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Compressed Gump  
Last Modified: 2009-09-10 16:06:57  
Modified By: Grin  
  
**Packet: 0xDD** 
Sent By: Server  
Size: variable  

  
**Packet Build**  
BYTE\[1\] Cmd  
BYTE\[2\] len  
BYTE\[4\] Player Serial  
BYTE\[4\] Gump ID  
BYTE\[4\] x  
BYTE\[4\] y  
BYTE\[4\] Compressed Gump Layout Length (CLen)  
BYTE\[4\] Decompressed Gump Layout Length (DLen)  
BYTE\[CLen-4\] Gump Data, zlib compressed  
BYTE\[4\] Number of text lines  
BYTE\[4\] Compressed Text Line Length (CTxtLen)  
BYTE\[4\] Decompressed Text Line Length (DTxtLen)  
BYTE\[CTxtLen-4\] Gump's Compressed Text data, zlib compressed  

  
**Subcommand Build**  
N/A

  
**Notes**  
text lines is in Big-Endian Unicode formate, not NULL terminated  
loop:   
BYTE\[2\] Length  
BYTE\[Length\*2\] text  
endloop  
  
Default level for decompresion  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Update Mobile Status  
Last Modified: 2009-03-01 05:48:56  
Modified By: Turley  
  
**Packet: 0xDE** 
Sent By: Server  
Size: variable  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[2\] Packet Length  
BYTE\[4\] Player Serial  
BYTE\[1\] Status  
**If status is 1**  
* BYTE\[4\] Attacker Serial?

  
**Subcommand Build**  
N/A

  
**Notes**  
Packet exists since buff/debuff system was published.  
  
**Status**  
0: Normal  
1: Being Attacked  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Buff/Debuff System  
Last Modified: 2008-10-18 11:46:59  
Modified By: Pierce  
  
**Packet: 0xDF** 
Sent By: Server  
Size: variable  

  
**Packet Build**  
BYTE\[1\] Cmd  
BYTE\[2\] Length  
BYTE\[4\] Serial of player  
BYTE\[2\] Icon Number to show  
BYTE\[2\] 0x1 = Show, 0x0 = Remove. On remove byte, packet ends here.  
BYTE\[4\] 0x00000000  
BYTE\[2\] Icon Number to show.  
BYTE\[2\] 0x1 = Show  
BYTE\[4\] 0x00000000  
BYTE\[2\] Time in seconds (simple countdown without automatic remove)  
BYTE\[2\] 0x0000  
BYTE\[1\] 0x00  
BYTE\[4\] Cliloc message ID1  
BYTE\[4\] Cliloc message ID2  
BYTE\[4\] 0x00000000  
BYTE\[2\] 0x0001  
BYTE\[len(str)\*2\] Flipped Unicode String (" "+str) (To seperate the entrys add " ")  
BYTE\[2\] 0x0000  

  
**Subcommand Build**  
N/A

  
**Notes**  
This is a submitted packet, information here is yet to be verified. Use at own risk.   
  
List of current Icon Names:  
(cliloc message numbers in brackets)  
  
1001: Dismount (cliloc1=1075635,cliloc2=1075636)  
1002: Disarm (cliloc1=1075637,cliloc2=1075638)  
1005: Nightsight (cliloc1=1075643,cliloc2=1075644)  
1006: Death Strike   
1007: Evil Omen   
1008: unknown? (GumpID 0x7556)  
1009: Regeneration (cliloc1=1044106,cliloc2=1075106)  
1010: Divine Fury   
1011: Enemy Of One   
1012: Stealth (cliloc1=1044107,cliloc2=1075655)  
1013: Active Meditation (cliloc1=1044106,cliloc2=1075106)  
1014: Blood Oath caster   
1015: Blood Oath curse   
1016: Corpse Skin   
1017: Mindrot   
1018: Pain Spike   
1019: Strangle   
1020: Gift of Renewal   
1021: Attune Weapon   
1022: Thunderstorm   
1023: Essence of Wind   
1024: Ethereal Voyage   
1025: Gift Of Life   
1026: Arcane Empowerment   
1027: Mortal Strike   
1028: Reactive Armor (cliloc1=1075812,cliloc2=1075813)  
1029: Protection (cliloc1=1075814,cliloc2=1075815)  
1030: Arch Protection (cliloc1=1075816,cliloc2=1075816)  
1031: Magic Reflection (cliloc1=1075817,cliloc2=1075818)  
1032: Incognito (cliloc1=1075819,cliloc2=1075820)  
1033: Disguised   
1034: Animal Form   
1035: Polymorph (cliloc1=1075824,cliloc2=1075820)  
1036: Invisibility (cliloc1=1075825,cliloc2=1075826)  
1037: Paralyze (cliloc1=1075827,cliloc2=1075828)  
1038: Poison (cliloc1=0x0F8627, cliloc2=0x1069B1)  
1039: Bleed (cliloc1=0x106a75,cliloc2=0x106a76)  
1040: Clumsy (cliloc1=0x106a77,cliloc2=0x106a78)  
1041: Feeble Mind (cliloc1=0x106a79,cliloc2=0x106a7a)  
1042: Weaken (cliloc1=1075837,cliloc2=1075838)  
1043: Curse (cliloc1=1075835,cliloc2=1075836)  
1044: Mass Curse (cliloc1=0x106a7f,cliloc2=0x106a80)  
1045: Agility (cliloc1=0x106a81,cliloc2=0x106a82)  
1046: Cunning (cliloc1=0x106a83,cliloc2=0x106a84)  
1047: Strength (cliloc1=0x106a85,cliloc2=0x106a86)  
1048: Bless (cliloc1=0x106a87,cliloc2=0x106a88)  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Mobile status/Animation update (KR)  
Last Modified: 2009-02-28 19:17:40  
Modified By: MuadDib  
  
**Packet: 0xE2** 
Sent By: Server  
Size: 10 bytes  

  
**Packet Build**  
BYTE\[1\] Command  
BYTE\[4\] Mobile serial  
BYTE\[2\] Action  
BYTE\[1\] unknown (0x00)  
BYTE\[1\] Secondary/Sub Action  
BYTE\[1\] Unknown  

  
**Subcommand Build**  
N/A

  
**Notes**  
Last byte appears a random number from 0 to 59\. Could this be a loop or timer?  

  
---

  
[\[^\]](#TOP)

**Packet Name:** KR Encryption Response  
Last Modified: 2009-08-02 20:49:25  
Modified By: MuadDib  
  
**Packet: 0xE3** 
Sent By: Server  
Size: 77 bytes  

  
**Packet Build**  
BYTE\[1\] 0xE3  
BYTE\[2\] Length  
BYTE\[4\] Key 1 Length  
BYTE\[Key 1 Length\] Key 1  
BYTE\[4\] Key 2 Length  
BYTE\[Key 2 Length\] Key 2  
BYTE\[4\] Key 3 Length  
BYTE\[Key 3 Length\] Key 3  
BYTE\[4\] Unknown  
BYTE\[4\] Key 4 Length  
BYTE\[Key 4 Length\] Key 4  

  
**Subcommand Build**  
N/A

  
**Notes**  
Unverified encryption method of keys is **AES in CFB Mode**  
  
As of 08/02/2009, this is the current encryption known for KR clients up to date:  
0xe3,  
0x4d00,  
0x03000000,  
0x02, 0x01, 0x03,  
0x13000000,  
0x02, 0x11, 0x00, 0xfc, 0x2f, 0xe3, 0x81, 0x93, 0xcb, 0xaf, 0x98, 0xdd, 0x83, 0x13, 0xd2, 0x9e, 0xea, 0xe4, 0x13,  
0x10000000,  
0x78, 0x13, 0xb7, 0x7b, 0xce, 0xA8, 0xd7, 0xbc, 0x52, 0xde, 0x38, 0x30, 0xea, 0xe9, 0x1e, 0xa3,  
0x20000000,  
0x10000000,  
0x5a, 0xce, 0x3e, 0xe3, 0x97, 0x92, 0xe4, 0x8a, 0xf1, 0x9a, 0xd3, 0x04, 0x41, 0x03, 0xcb, 0x53  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Krrios client special  
Last Modified: 2009-03-02 05:22:34  
Modified By: Turley  
  
**Packet: 0xF0** 
Sent By: Server  
Size: Variable  

  
**Packet Build**  
BYTE\[2\] len  
BYTE\[1\] subcmd  
BYTE\[len - 4\] data  

  
**Subcommand Build**  
N/A

  
**Notes**  
Subcommand 0: Acknowledge login (1 byte (for 5 total))  
  
BYTE\[1\] ack (0 = Rejected, unauthorized. 1 = Accepted, GM priviledges. 2 = Accepted, player priviledges)  
  
  
Note: GM priviledges unlock movement rate configurations  

  
---

  
[\[^\]](#TOP)

**Packet Name:** Object Information (SA)  
Last Modified: 2009-09-18 07:45:23  
Modified By: Tomi  
  
**Packet: 0xF3** 
Sent By: Server  
Size: 24 bytes  

  
**Packet Build**  
Byte\[1\] Packet ID  
Byte\[2\] 0x1 // always 0x1 on OSI  
Byte\[1\] DataType // 0x00 = Item , 0x02 = Multi  
Byte\[4\] Serial  
Byte\[2\] Graphic // for multi its same value as the multi has in multi.mul  
Byte\[1\] Facing // 0x00 if Multi  
Byte\[2\] Amount // 0x1 if Multi  
Byte\[2\] Amount // 0x1 if Multi , no idea why Amount is sent 2 times  
Byte\[2\] X  
Byte\[2\] Y  
Byte\[1\] Z  
Byte\[1\] Layer // 0x00 if Multi  
Byte\[2\] Color // 0x00 if Multi  
Byte\[1\] Flag // 0x20 = Show Properties , 0x80 = Hidden , 0x00 if Multi  

  
**Subcommand Build**  
N/A

  
**Notes**  
Client versions >= 7.0.0.0 is sending this instead of the old Object Information packet 0x1A  
  
Seems like 0x1A is still working with item graphics <= 0x3FFF but OSI uses this now to send ALL.  

  
---

  
# Packets

[Show Online Version](http://docs.polserver.com/packets)  

[\[-\]](javascript:ExpandContract%28'Both'%29)

  
**Both**  
[\[0x15\] Follow](#0x15)  
[\[0x22\] Character Move ACK/ Resync Request](#0x22)  
[\[0x2C\] Resurrection Menu](#0x2C)  
[\[0x39\] Remove (Group)](#0x39)  
[\[0x3A\] Send Skills](#0x3A)  
[\[0x56\] Map Packet (cartography/treasure)](#0x56)  
[\[0x66\] Books (Pages)](#0x66)  
[\[0x6C\] Target Cursor Commands](#0x6C)  
[\[0x6F\] Secure Trading](#0x6F)  
[\[0x71\] Bulletin Board Messages](#0x71)  
[\[0x72\] Request War Mode](#0x72)  
[\[0x73\] Ping Message](#0x73)  
[\[0x93\] Book Header ( Old )](#0x93)  
[\[0x95\] Dye Window](#0x95)  
[\[0x98\] All Names (3D Client Only)](#0x98)  
[\[0x99\] Give Boat/House Placement View](#0x99)  
[\[0x9A\] Console Entry Prompt](#0x9A)  
[\[0xB3\] Chat Text](#0xB3)  
[\[0xB8\] Request/Char Profile](#0xB8)  
[\[0xBB\] Ultima Messenger](#0xBB)  
[\[0xBD\] Client Version](#0xBD)  
[\[0xBE\] Assist Version](#0xBE)  
[\[0xBF\] General Information Packet](#0xBF)  
[\[0xC2\] Unicode TextEntry](#0xC2)  
[\[0xC8\] Client View Range](#0xC8)  
[\[0xC9\] Get Area Server Ping](#0xC9)  
[\[0xCA\] Get User Server Ping](#0xCA)  
[\[0xD0\] Configuration File](#0xD0)  
[\[0xD1\] Logout Status](#0xD1)  
[\[0xD4\] Book Header ( New )](#0xD4)  
[\[0xD6\] Mega Cliloc](#0xD6)  
[\[0xD7\] Generic AOS Commands](#0xD7)  
[\[0xF1\] Freeshard List](#0xF1)  

[\[-\]](javascript:ExpandContract%28'Both?'%29)

  
**Both?**  
[\[0x0C\] Edit Tile Data (God Client)](#0x0C)  

[\[-\]](javascript:ExpandContract%28'Client'%29)

  
**Client**  
[\[0x00\] Create Character](#0x00)  
[\[0x01\] Disconnect Notification](#0x01)  
[\[0x02\] Move Request](#0x02)  
[\[0x03\] Talk Request](#0x03)  
[\[0x04\] Request God Mode (God Client)](#0x04)  
[\[0x05\] Request Attack](#0x05)  
[\[0x06\] Double Click](#0x06)  
[\[0x07\] Pick Up Item](#0x07)  
[\[0x08\] Drop Item](#0x08)  
[\[0x09\] Single Click](#0x09)  
[\[0x0A\] Edit (God Client)](#0x0A)  
[\[0x12\] Request Skill etc use](#0x12)  
[\[0x13\] Drop->Wear Item](#0x13)  
[\[0x14\] Send Elevation (God Client)](#0x14)  
[\[0x16\] Request Script Names](#0x16)  
[\[0x1E\] Control Animation](#0x1E)  
[\[0x34\] Get Player Status](#0x34)  
[\[0x35\] Add Resource (God Client)](#0x35)  
[\[0x37\] Move Item (God Client)](#0x37)  
[\[0x38\] Pathfinding in Client](#0x38)  
[\[0x3B\] Buy Item(s)](#0x3B)  
[\[0x45\] Version OK](#0x45)  
[\[0x46\] New Artwork](#0x46)  
[\[0x47\] New Terrain](#0x47)  
[\[0x48\] New Animation](#0x48)  
[\[0x49\] New Hues](#0x49)  
[\[0x4A\] Delete Art](#0x4A)  
[\[0x4B\] Check Client Version](#0x4B)  
[\[0x4C\] Script Names](#0x4C)  
[\[0x4D\] Edit Script File](#0x4D)  
[\[0x50\] Board Header](#0x50)  
[\[0x51\] Board Message](#0x51)  
[\[0x52\] Board Post Message](#0x52)  
[\[0x57\] Update Regions](#0x57)  
[\[0x58\] Add Region](#0x58)  
[\[0x59\] New Context FX](#0x59)  
[\[0x5A\] Update Context FX](#0x5A)  
[\[0x5C\] Restart Version](#0x5C)  
[\[0x5D\] Login Character](#0x5D)  
[\[0x5E\] Server Listing](#0x5E)  
[\[0x5F\] Server List Add Entry](#0x5F)  
[\[0x60\] Server List Remove Entry](#0x60)  
[\[0x61\] Remove Static Object](#0x61)  
[\[0x62\] Move Static Object](#0x62)  
[\[0x63\] Load Area](#0x63)  
[\[0x64\] Load Area Request](#0x64)  
[\[0x69\] Change Text/Emote Colors](#0x69)  
[\[0x75\] Rename Character](#0x75)  
[\[0x7D\] Response To Dialog Box](#0x7D)  
[\[0x80\] Login Request](#0x80)  
[\[0x83\] Delete Character](#0x83)  
[\[0x8D\] Character Creation ( KR + SA 3D clients only )](#0x8D)  
[\[0x91\] Game Server Login](#0x91)  
[\[0x97\] Move Player](#0x97)  
[\[0x9B\] Request Help](#0x9B)  
[\[0x9F\] Sell List Reply](#0x9F)  
[\[0xA0\] Select Server](#0xA0)  
[\[0xA4\] Client Spy](#0xA4)  
[\[0xA7\] Request Tip/Notice Window](#0xA7)  
[\[0xAC\] Gump Text Entry Dialog Reply](#0xAC)  
[\[0xAD\] Unicode/Ascii speech request](#0xAD)  
[\[0xB1\] Gump Menu Selection](#0xB1)  
[\[0xB5\] Open Chat Window](#0xB5)  
[\[0xB6\] Send Help/Tip Request](#0xB6)  
[\[0xC5\] Invalid Map (Request?)](#0xC5)  
[\[0xD9\] Spy On Client](#0xD9)  
[\[0xE0\] Bug Report (KR)](#0xE0)  
[\[0xE1\] Client Type (KR/SA)](#0xE1)  
[\[0xEC\] Equip Macro (KR)](#0xEC)  
[\[0xED\] Unequip Item Macro (KR)](#0xED)  
[\[0xEF\] KR/2D Client Login/Seed](#0xEF)  

[\[-\]](javascript:ExpandContract%28'Server'%29)

  
**Server**  
[\[0x0B\] Damage](#0x0B)  
[\[0x11\] Status Bar Info](#0x11)  
[\[0x17\] Health bar status update (KR)](#0x17)  
[\[0x1A\] Object Info](#0x1A)  
[\[0x1B\] Char Locale and Body](#0x1B)  
[\[0x1C\] Send Speech](#0x1C)  
[\[0x1D\] Delete Object](#0x1D)  
[\[0x1F\] Explosion](#0x1F)  
[\[0x20\] Draw Game Player](#0x20)  
[\[0x21\] Char Move Rejection](#0x21)  
[\[0x23\] Dragging Of Item](#0x23)  
[\[0x24\] Draw Container](#0x24)  
[\[0x25\] Add Item To Container](#0x25)  
[\[0x26\] Kick Player](#0x26)  
[\[0x27\] Reject Move Item Request](#0x27)  
[\[0x28\] Drop Item Failed/Clear Square (God Client?)](#0x28)  
[\[0x29\] Drop Item Approved](#0x29)  
[\[0x2A\] Blood](#0x2A)  
[\[0x2B\] God Mode (God Client)](#0x2B)  
[\[0x2D\] Mob Attributes](#0x2D)  
[\[0x2E\] Worn Item](#0x2E)  
[\[0x2F\] Fight Occuring](#0x2F)  
[\[0x30\] Attack Ok](#0x30)  
[\[0x31\] Attack Ended](#0x31)  
[\[0x32\] Unknown](#0x32)  
[\[0x33\] Pause Client](#0x33)  
[\[0x36\] Resource Tile Data (God Client](#0x36)  
[\[0x3C\] Add multiple Items In Container](#0x3C)  
[\[0x3E\] Versions (God Client)](#0x3E)  
[\[0x3F\] Update Statics (God Client)](#0x3F)  
[\[0x4E\] Personal Light Level](#0x4E)  
[\[0x4F\] Overall Light Level](#0x4F)  
[\[0x53\] Reject Character Logon](#0x53)  
[\[0x54\] Play Sound Effect](#0x54)  
[\[0x55\] Login Complete](#0x55)  
[\[0x5B\] Time](#0x5B)  
[\[0x65\] Set Weather](#0x65)  
[\[0x6D\] Play Midi Music](#0x6D)  
[\[0x6E\] Character Animation](#0x6E)  
[\[0x70\] Graphical Effect](#0x70)  
[\[0x74\] Open Buy Window](#0x74)  
[\[0x76\] New Subserver](#0x76)  
[\[0x77\] Update Player](#0x77)  
[\[0x78\] Draw Object](#0x78)  
[\[0x7C\] Open Dialog Box](#0x7C)  
[\[0x82\] Login Denied](#0x82)  
[\[0x86\] Resend Characters After Delete](#0x86)  
[\[0x88\] Open Paperdoll](#0x88)  
[\[0x89\] Corpse Clothing](#0x89)  
[\[0x8C\] Connect To Game Server](#0x8C)  
[\[0x90\] Map Message](#0x90)  
[\[0x9C\] Request Assistance](#0x9C)  
[\[0x9E\] Sell List](#0x9E)  
[\[0xA1\] Update Current Health](#0xA1)  
[\[0xA2\] Update Current Mana](#0xA2)  
[\[0xA3\] Update Current Stamina](#0xA3)  
[\[0xA5\] Open Web Browser](#0xA5)  
[\[0xA6\] Tip/Notice Window](#0xA6)  
[\[0xA8\] Game Server List](#0xA8)  
[\[0xA9\] Characters / Starting Locations](#0xA9)  
[\[0xAA\] Allow/Refuse Attack](#0xAA)  
[\[0xAB\] Gump Text Entry Dialog](#0xAB)  
[\[0xAE\] Unicode Speech message](#0xAE)  
[\[0xAF\] Display Death Action](#0xAF)  
[\[0xB0\] Send Gump Menu Dialog](#0xB0)  
[\[0xB2\] Chat Message](#0xB2)  
[\[0xB7\] Help/Tip Data](#0xB7)  
[\[0xB9\] Enable locked client features](#0xB9)  
[\[0xBA\] Quest Arrow](#0xBA)  
[\[0xBC\] Seasonal Information](#0xBC)  
[\[0xC0\] Graphical Effect](#0xC0)  
[\[0xC1\] Cliloc Message](#0xC1)  
[\[0xC4\] Semivisible (Smurf it!)](#0xC4)  
[\[0xC6\] Invalid Map Enable](#0xC6)  
[\[0xC7\] 3D Particle Effect](#0xC7)  
[\[0xCB\] Global Que Count](#0xCB)  
[\[0xCC\] Cliloc Message Affix](#0xCC)  
[\[0xD2\] Extended 0x20](#0xD2)  
[\[0xD3\] Extended 0x78](#0xD3)  
[\[0xD8\] Send Custom House](#0xD8)  
[\[0xDB\] Character Transfer Log](#0xDB)  
[\[0xDC\] SE Introduced Revision](#0xDC)  
[\[0xDD\] Compressed Gump](#0xDD)  
[\[0xDE\] Update Mobile Status](#0xDE)  
[\[0xDF\] Buff/Debuff System](#0xDF)  
[\[0xE2\] Mobile status/Animation update (KR)](#0xE2)  
[\[0xE3\] KR Encryption Response](#0xE3)  
[\[0xF0\] Krrios client special](#0xF0)  
[\[0xF3\] Object Information (SA)](#0xF3)  

 Copyright � 1998-2010 POL Development Team.