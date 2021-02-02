set(src
  auditd_file.c
  auditd_dockermode.c
  auditd_scan.c
  auditd_config.c
  auditd_fs.c
  auditd.c)

if(FLB_SQLDB)
set(src
    ${src}
    auditd_db.c)
endif()

if(FLB_PARSER)
  set(src
    ${src}
    auditd_multiline.c
    )
endif()

if(MSVC)
  FLB_PLUGIN(in_auditd "${src}" "Shlwapi")
else()
  FLB_PLUGIN(in_auditd "${src}" "")
endif()