    v4 = hid_read_timeout(v3, &v34, 16i64);

    if ( v4 == 16 || v4 == 32 )

    {

      v8 = (float)v35 * 0.000061035156;

      v9 = (float)v36 * 0.000061035156;

      v10 = (float)v37 * 0.000061035156;

      v11 = (float)v38 * 0.000061035156;

      v12 = sinf(0.78539819);

      v13 = v12;

      v33 = v12;

      v14 = cosf(0.78539819);

      v15 = (float)((float)((float)(v13 * v11) + (float)(v14 * v8)) + (float)(v9 * 0.0)) - (float)(v10 * 0.0);

      v16 = (float)((float)((float)(v8 * 0.0) + (float)(v11 * 0.0)) - (float)(v13 * v9)) + (float)(v14 * v10);

      v17 = (float)((float)((float)(v14 * v11) - (float)(v13 * v8)) - (float)(v9 * 0.0)) - (float)(v10 * 0.0);

      v18 = -(float)((float)((float)((float)(v11 * 0.0) - (float)(v8 * 0.0)) + (float)(v14 * v9)) + (float)(v13 * v10));

      v19 = sqrtf((float)((float)((float)(v16 * v16) + (float)(v15 * v15)) + (float)(v18 * v18)) + (float)(v17 * v17));

      if ( v19 != 0.0 )

        v19 = 1.0 / v19;

      v20 = (unsigned __int128)_mm_unpacklo_ps((__m128)0i64, (__m128)0i64);

      *(float *)&v24 = v15 * v19;

      *((float *)&v24 + 1) = v16 * v19;

      *((_QWORD *)&v24 + 1) = __PAIR64__(v17 * v19, v18 * v19);

      v21 = __readgsqword(0x58u);

      v22 = *(_QWORD *)v21;

      if ( dword_1400F7B00 > *(_DWORD *)(*(_QWORD *)v21 + 4i64) )

      {

        Init_thread_header(&dword_1400F7B00);

        if ( dword_1400F7B00 == -1 )

        {

          qword_1400F79D0 = (__int64)&PvrService::`vftable';

          Init_thread_footer(&dword_1400F7B00);

        }

      }

      v25 = v20;

      v26 = 0;

      v27 = 0i64;

      v28 = 0;

      v29 = 0i64 >> 63;

      *(_QWORD *)&v30 = 0i64;

      DWORD2(v30) = 0;

      *(_QWORD *)&v31 = sub_14005B330(v22);

      DWORD2(v31) = 1;

      v23 = 96i64 * (~(unsigned __int8)_InterlockedExchangeAdd((volatile signed __int32 *)(v2 + 232), 1u) & 1);

      *(_OWORD *)(v23 + v2 + 240) = v24;

      *(_OWORD *)(v23 + v2 + 256) = *(_OWORD *)&v25;

      *(_OWORD *)(v23 + v2 + 272) = *(_OWORD *)((char *)&v27 + 4);

      *(_OWORD *)(v23 + v2 + 288) = 0i64;

      *(_OWORD *)(v23 + v2 + 304) = v30;

      *(_OWORD *)(v23 + v2 + 320) = v31;

      result = (unsigned int)_InterlockedExchangeAdd((volatile signed __int32 *)(v2 + 236), 1u);

    }

    else

    {

      v6 = sub_1400123A0(v5);

      v7 = (_BYTE *)sub_140012430(v6, &v32, 5i64);

      if ( *v7 )

        sub_140006960(v7 + 16, "read_len wrong.", 0xFui64);

      result = sub_1400120A0(&v32);

    }

  }

  return result;

}
