;@xkas
; MSU1 asm by Conn, many thanks to Ikari_01, EmuandCo, Kiddo, etc 07/04/2015
; use on Super Mario World (US) with header

!Volume = $FF      ; full volume, change FF->60 when playing in bsnes in case the volume is too loud       
!FadeSpeed = $08     ; fade speed, chnage 08->04 if volume set to 60

header
lorom

org $00810E 		; jingle fix
	JSL FadeScreen
	NOP

org $008186 
	JSL MSU1	; hook
	NOP #5

org $009E17
	JSL FadeMusic
	NOP

org $00A23B		; pause game stop msu
	JSL PauseMSU

org $0491E0
	JSL FadeMusic
	NOP

org $04EF3E		; Empty rom adress aka freespace
	db "STAR"
	dw RatstagFinish-RatstagBegin-$01
	dw RatstagFinish-RatstagBegin-$01^$FFFF

RatstagBegin:
MSU1:
	LDA $2002      ;check if msu found
	CMP #$53               
	BEQ msufound
	LDA $1DFB  
	STA $2142      ;repeat deleted native code
	STA $1DFF    
	STZ $1DFB      ;reset track change ram  
	RTL      
  
msufound: 
	LDA $1DFB
	BNE $01
	RTL
	CMP #$80
	BEQ Fade
	LDA #$80      ;mute spc          
	STA $2142  
	STA $1DFF  
	LDA $1e00     ;check "MSU1 busy pending" flag
	BNE checkmsuready
	JMP SwitchBank

continue1:
	LDA $1DFB	; load track#

continue2:
	STA $2004       ; play msu track
	STZ $2005 
	LDA #$01        ; set "MSU1 busy pending" flag
	STA $1E00
	RTL

Fade:
	LDA $0087
	BEQ end
	SEC
	SBC #!FadeSpeed
	CMP #$0A
	BCC EndFade
	STA $0087
	STA $2006
	RTL

EndFade:
	STZ $0087
	STZ $2007
	RTL

checkmsuready:
	BIT $2000 
	BVS end       ; if not ready loop
	STZ $1E00
	JSR TrackLoop
	STA $2007 
	STA $1DEF     ; save value for pausing

fullvolume:
	LDA #!Volume
	STA $2006

testerrorbit:
	LDA $2000     
	AND #$08      ; check error bit for spc fallback (only featured bsnes 0.8 and higher as well as sdnes)
	BNE playspc       
	STZ $1DFB

end:
	RTL

playspc:              ; only if msu track not found
	STZ $2007
	LDA $1DFB  
	STA $2142  
	STA $1DFF  
	STZ $1DFB 
	RTL

;;;;;;;;;;;;;;;;;;;;;;;; Select if track is looped. All tracks which are sfx are not looped: http://www.smwcentral.net/?p=viewthread&t=6665

TrackLoop:
	LDA $1DFB 
	CMP #$08       ; check head bowser valley entrance theme
	BNE proceed
	LDA $D0        ; check overworld
	Beq $03
	LDA #$03              
	RTS
	LDA #$01       ; we are at overworld so don't loop              
	RTS  

proceed:
	CMP #$09               	;mario died
	BNE proceed2    		
	LDA $D0 		; check overworld
	Beq $03
	LDA #$01              
	RTS
	LDA $70
	CMP #$7E
	BNE secrets
	LDA #$01    		; we are at credits              
	RTS

secrets:
	LDA #$03              
	RTS           

proceed2:

	CMP #$0A             	; game over
	BNE $03   
	LDA #$01        
	RTS                 
	CMP #$0B             	; passed boss
	BNE $03   
	LDA #$01         
	RTS              
	CMP #$0C  		; passed Level      
	BNE $03    
	LDA #$01       
	RTS                
	CMP #$0F   		; Into keyhole       
	BNE $03    
	LDA #$01       
	RTS            
	CMP #$10   		; into keyhole       
	BNE $03    
	LDA #$01          
	RTS              
	CMP #$11        	; Zoom in
	BNE $03    
	LDA #$01         
	RTS              
	CMP #$13           	; Welcome!
	BNE $03    
	LDA #$01         
	RTS             
	CMP #$14         	;Done bonus game
	BNE $03   
	LDA #$01         
	RTS            
	CMP #$15        	;rescue egg
	BNE $03   
	LDA #$01        
	RTS             
	CMP #$17           	;Bowser Zoom out
	BNE $03    
	LDA #$01        
	RTS              
	CMP #$18   		;bowser zoom in        
	BNE $03   
	LDA #$01         
	RTS            
	CMP #$1B  		;Bowser died      
	BNE $03    
	LDA #$01          
	RTS              
	CMP #$1C   		;Princess kiss     
	BNE $03    
	LDA #$01         
	RTS             
	CMP #$1D         	;Bowser Interlude
	BNE $03    
	LDA #$01 
	RTS
	LDA #$03         	;loop all other tracks
	RTS

;;;;;;;;;;;;;;;;;;;;;;;; check if music bank needs switched

SwitchBank:
	LDA $D0
	BEQ overworld
	LDA $04A0
	BNE level
	LDA $04A1
	BNE level

overworld:  
	LDA $1DFB
	CMP #$01   		;title screen     
	BNE $05    
	LDA #$1E         
	JMP continue2 
	CMP #$02   		;ow2     
	BNE $05    
	LDA #$1F         
	JMP continue2
	CMP #$03   		;ow3     
	BNE $05    
	LDA #$20         
	JMP continue2
	CMP #$04   		;ow4     
	BNE $05    
	LDA #$21         
	JMP continue2
	CMP #$05   		;ow5     
	BNE $05    
	LDA #$22         
	JMP continue2 
	CMP #$06   		;ow6     
	BNE $05    
	LDA #$23         
	JMP continue2 
	CMP #$07   		;ow7     
	BNE $05    
	LDA #$24         
	JMP continue2 
	CMP #$08   		;ow8     
	BNE $05    
	LDA #$25         
	JMP continue2  
	CMP #$09   		;ow9     
	BNE level    
	LDA $70
	CMP #$7E
	BNE ow
	LDA #$27
	JMP continue2

ow:
	LDA #$26         
	JMP continue2 

level:
	LDA $1DFB
	CMP #$0A
	BNE playend2
	LDA $70
	CMP #$7E           ; are we at credits?
	BEQ $03
	JMP continue1
	LDA #$28
	JMP continue2

playend2:
	CMP #$0B
	BEQ $03
	JMP continue1
	LDA $70
	CMP #$7E           ; are we at credits?
	BEQ $03
	JMP continue1
	LDA #$29
	JMP continue2

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PauseMSU:
	BEQ $02
	LDY #$11
	LDA $2002          ;check if msu found
	CMP #$53               
	BEQ $01
	RTL
	CPY #$11
	BNE pauseend
	STZ $2007
	RTL

pauseend:
	CPY #$12
	BNE end2
	LDA $1DEF
	STA $2007

end2:
	RTL

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

FadeScreen:
	LDA #$B1
	STA $0000
	LDA $2002          ;check if msu found
	CMP #$53               
	BEQ $01
	RTL
	LDA $1DEF
	cmp #$01
	BEQ jingle
	STZ $2007
	RTL

jingle:
	LDA $2000
	and #$10
	BNE jingle
	RTL

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

FadeMusic:
	LDA $2002
	CMP #$53
	BNE Finish
	LDA $0087
	BNE Mute
	LDA #!Volume
	STA $0087

Mute:
	LDA #$80
	STA $2142
	STA $1DFF
	STA $1DFB
	RTL

Finish:
	LDA #$80
	STA $1DFB
	RTL

RatstagFinish:
print "bytes inserted: ",bytes