function element_0(shader, t_base, t_second, t_detail)
	shader:begin("stub_notransform_build", "bloom_build")
		:fog(false)
		:zb(false, false)
        :blend(false, blend.srcalpha,blend.invsrcalpha)
	shader:dx10texture("s_image", "$user$generic0")
	shader:dx10texture("s_tonemap", "$user$tonemap")
	shader:dx10sampler("smp_rtlinear")
end
