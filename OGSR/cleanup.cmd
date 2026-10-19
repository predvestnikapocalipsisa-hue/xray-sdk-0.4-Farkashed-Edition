RD /s /q 3rd_party\_LIB
RD /s /q 3rd_party\_TEMP
RD /s /q ogsr_engine\_LIB
RD /s /q ogsr_engine\_TEMP
RD /s /q ogsr_engine\LuaJIT\bin
RD /s /q bin_x86\
RD /s /q bin_x64\
for /d %%A in (.vs\Engine\*) do (
	del %%A\*.VC.*
	del %%A\*.bin*
	RD /s /q %%A\ipch
	for /d %%B in (%%A\*) do (
		del %%B\*.VC.*
		del %%B\*.bin*
		RD /s /q %%B\ipch
	)
)
pause
