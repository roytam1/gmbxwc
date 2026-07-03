rem Shift-JIS (CP932)
python build_cpbl.py CP932.TXT CP932.DAT -stateless -bin 932

rem GBK (CP936)
python build_cpbl.py CP936.TXT CP936.DAT -stateless -bin 936

rem EUC-KR (CP949)
python build_cpbl.py CP949.TXT CP949.DAT -stateless -bin 949

rem Big5 (UAO 2.50) (CP950)
python build_cpbl.py CP950.TXT CP950.DAT -stateless -bin 950

rem Big5-HKSCS-2008 (CP951)
python build_cpbl.py HKSCS2K8.TXT HKSCS2K8.DAT -stateless -bin 951

rem GB18030 (CP54936)
python build_cpbl_ucm.py gb18030-2022.ucm GB18030.DAT -gb18030 -bin 54936

rem IBM EBCDIC Traditional Chinese Host miexed with 6204 UDC, superset of 5033
python build_cpbl_ucm.py ibm-937_P110-1999.ucm IBM937.DAT -ebcdic -bin 21937

rem IBM EBCDIC Japanese Latin Kanji mixed with 4370 UDC, superset of 5035
python build_cpbl_ucm.py ibm-939_P120-1999.ucm IBM939.DAT -ebcdic -bin 21939

rem IBM EBCDIC Japanese Katakana-Kanji mixed with 4370 UDC, superset of 5026
python build_cpbl_ucm.py ibm-930_P120-1999.ucm IBM930.DAT -ebcdic -bin 21930

rem IBM EBCDIC Korean Mixed with 1880 UDC, superset of 5029
python build_cpbl_ucm.py ibm-933_P110-1995.ucm IBM933.DAT -ebcdic -bin 21933

rem IBM EBCDIC Simplified Chinese Host mixed with 1880 UDC, superset of 5031
python build_cpbl_ucm.py ibm-935_P110-1999.ucm IBM935.DAT -ebcdic -bin 21935
